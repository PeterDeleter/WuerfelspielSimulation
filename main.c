#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include <windows.h>
#include <psapi.h>
#include <io.h>
#include "structs.h"
#include "khash.h"

int num_threads;

uint16_t transitions[1296][4];

uint8_t trans_normalized_sorted[56][5];
uint8_t trans_normalized[155][5];
uint8_t trans[460][5];
uint16_t n_tns = 0;
uint16_t n_tn = 0;
uint16_t n_t = 0;
int depth;

void static inline normalize(uint16_t *vector){
    uint16_t min = (vector[0] < vector[1]) ? vector[0] : vector[1];
    uint16_t min2 = (vector[2] < vector[3]) ? vector[2] : vector[3];
    min = (min < min2) ? min : min2;
    vector[0] -= min;
    vector[1] -= min;
    vector[2] -= min;
    vector[3] -= min;
}

void initialize_transitions(){
    int m = 0;
    for (int i = 0; i < 6; i++){
        for (int j = 0; j < 6; j++){
            for (int k = 0; k < 6; k++){
                for (int l = 0; l < 6; l++){
                    transitions[m][0] = i;
                    transitions[m][1] = j;
                    transitions[m][2] = k;
                    transitions[m][3] = l;
                    //printf("[%i, %i, %i, %i] ", transitions[m][0], transitions[m][1], transitions[m][2], transitions[m][3]);
                    normalize(transitions[m]);
                    //printf("[%i, %i, %i, %i]\n", transitions[m][0], transitions[m][1], transitions[m][2], transitions[m][3]);
                    m += 1;
                }
            }
        }
    }
    for (int i = 0; i < 1296; i++){
        uint8_t inserted = 0;
        if (transitions[i][0] == 0){
            if (transitions[i][1] <= transitions[i][2] && transitions[i][2] <= transitions[i][3]){
                for (int j = 0; j < n_tns; j++){
                    if (trans_normalized_sorted[j][0] == transitions[i][0] &&
                        trans_normalized_sorted[j][1] == transitions[i][1] &&
                        trans_normalized_sorted[j][2] == transitions[i][2] &&
                        trans_normalized_sorted[j][3] == transitions[i][3]){
                            trans_normalized_sorted[j][4]++;
                            inserted = 1;
                            break;
                    }
                }
                if (!inserted){
                    for (int j = 0; j < 4; j++)
                        trans_normalized_sorted[n_tns][j] = transitions[i][j];
                    trans_normalized_sorted[n_tns][4] = 1;
                    n_tns++;
                    inserted = 0;
                }
            }
            else{
                for (int j = 0; j < n_tn; j++){
                    if (trans_normalized[j][0] == transitions[i][0] &&
                        trans_normalized[j][1] == transitions[i][1] &&
                        trans_normalized[j][2] == transitions[i][2] &&
                        trans_normalized[j][3] == transitions[i][3]){
                            trans_normalized[j][4]++;
                            inserted = 1;
                            break;
                    }
                }
                if (!inserted){
                    for (int j = 0; j < 4; j++)
                        trans_normalized[n_tn][j] = transitions[i][j];
                    trans_normalized[n_tn][4] = 1;
                    n_tn++;
                    inserted = 0;
                }
            }
        }
        else{
            for (int j = 0; j < n_t; j++){
                if (trans[j][0] == transitions[i][0] &&
                    trans[j][1] == transitions[i][1] &&
                    trans[j][2] == transitions[i][2] &&
                    trans[j][3] == transitions[i][3]){
                        trans[j][4]++;
                        inserted = 1;
                        break;
                }
            }
            if (!inserted){
                for (int j = 0; j < 4; j++)
                    trans[n_t][j] = transitions[i][j];
                trans[n_t][4] = 1;
                n_t++;
                inserted = 0;
            }
        }
    }
    //printf("n_tns: %i\nn_tn: %i\nn_t: %i\n", n_tns, n_tn, n_t);
}

// init state map

static inline int state_equal(State a, State b){
    return memcmp(&a, &b, sizeof(State)) == 0;
}

static inline khint_t state_hash(State s){
    khint_t h = 1469598103934665603ULL;

    for (int i = 0; i < 4; i++) {
        h ^= s.state[i];
        h *= 1099511628211ULL;
    }

    return h;
}

KHASH_INIT(
    state_map,      // name
    State,          // key type
    __uint128_t,    // value type
    1,              // map, not set
    state_hash,
    state_equal
)

khash_t(state_map) *current;

__uint128_t n_wins;
__uint128_t n_games;

float pct;
float cum_pct;
float bytes_per_state;

__uint128_t *local_wins;
__uint128_t *local_games;
khash_t(state_map) **local_next;



// helper functions

void static inline sort4(uint16_t *vector){                       // sorts 4 element vectors, MUST be normalized
    uint16_t sorted[4] = {0, 0, 0, 0};
    uint16_t buffer;
    uint8_t k = 3;
    for (uint8_t i = 0; i < 4; i++){
        if (vector[i] != 0){
            sorted[k] = vector[i];
            k--;
        }
    }
    if (k == 0){                                    // sorted[1:3] nonzero and unsorted
        if (sorted[2] < sorted[1]){
            buffer = sorted[1];
            sorted[1] = sorted[2];
            sorted[2] = buffer;
        }
        if (sorted[3] < sorted[1]){
            buffer = sorted[1];
            sorted[1] = sorted[3];
            sorted[3] = sorted[2];
            sorted[2] = buffer;
        }
        if (sorted[3] < sorted[2]){
            buffer = sorted[2];
            sorted[2] = sorted[3];
            sorted[3] = buffer;
        }
    }
    else if (k == 1){                                // sorted[2:3] nonzero and unsorted
        if (sorted[3] < sorted[2]){
            buffer = sorted[2];
            sorted[2] = sorted[3];
            sorted[3] = buffer;
        }
    }
    vector[0] = sorted[0];
    vector[1] = sorted[1];
    vector[2] = sorted[2];
    vector[3] = sorted[3];
}

int static inline is_won(uint16_t *vector){
    return (vector[2] == 0 || vector[1] == vector[3]);
}

void static inline shift(khash_t(state_map) *state, __uint128_t n_states){
    //printf("\nshifting\n%i\n", n_states);
    if (!(n_states >> 117))
        return;

    int highest = 127;

    while (highest > 117 && ((n_states >> highest) & 1) == 0) {
        highest--;
    }

    int n_shifts = highest - 116;

    for (khiter_t k = kh_begin(state); k != kh_end(state); ++k) {
        if (!kh_exist(state, k))
            continue;

        kh_value(state, k) >>= n_shifts;
    }
}

void step(State state_current, __uint128_t multiplicity, int tid){
    State state_next;
    for (int i = 0; i < n_tns; i++){               // saves sorting and normalizing
        state_next.state[0] = state_current.state[0] + trans_normalized_sorted[i][0];
        state_next.state[1] = state_current.state[1] + trans_normalized_sorted[i][1];
        state_next.state[2] = state_current.state[2] + trans_normalized_sorted[i][2];
        state_next.state[3] = state_current.state[3] + trans_normalized_sorted[i][3];

        __uint128_t mul_next = multiplicity * trans_normalized_sorted[i][4];
        if (is_won(state_next.state)){
            local_wins[tid] += mul_next;
        }
        else{
            int ret;
            khiter_t key_next = kh_put(state_map, local_next[tid], state_next, &ret);
            if (ret > 0){
                kh_value(local_next[tid], key_next) = mul_next;
            }
            else{
                kh_value(local_next[tid], key_next) += mul_next;
            }
        }
        local_games[tid] += mul_next;
    }
    for (int i = 0; i < n_tn; i++){               // saves normalizing
        state_next.state[0] = state_current.state[0] + trans_normalized[i][0];
        state_next.state[1] = state_current.state[1] + trans_normalized[i][1];
        state_next.state[2] = state_current.state[2] + trans_normalized[i][2];
        state_next.state[3] = state_current.state[3] + trans_normalized[i][3];
        sort4(state_next.state);
        __uint128_t mul_next = multiplicity * trans_normalized[i][4];

        if (is_won(state_next.state)){
            local_wins[tid] += mul_next;
        }
        else{
            int ret;
            khiter_t key_next = kh_put(state_map, local_next[tid], state_next, &ret);
            if (ret > 0){
                kh_value(local_next[tid], key_next) = mul_next;
            }
            else{
                kh_value(local_next[tid], key_next) += mul_next;
            }
        }
        local_games[tid] += mul_next;
    }
    for (int i = 0; i < n_t; i++){               // saves nothing lol
        state_next.state[0] = state_current.state[0] + trans[i][0];
        state_next.state[1] = state_current.state[1] + trans[i][1];
        state_next.state[2] = state_current.state[2] + trans[i][2];
        state_next.state[3] = state_current.state[3] + trans[i][3];
        normalize(state_next.state);
        sort4(state_next.state);
        __uint128_t mul_next = multiplicity * trans[i][4];

        if (is_won(state_next.state)){ //Always won after round 1, however removing makes code slower :(
            local_wins[tid] += mul_next;
        }
        else{
            int ret;
            khiter_t key_next = kh_put(state_map, local_next[tid], state_next, &ret);
            if (ret > 0){
                kh_value(local_next[tid], key_next) = mul_next;
            }
            else{
                kh_value(local_next[tid], key_next) += mul_next;
            }
        }
        local_games[tid] += mul_next;
    }
}

// post step functions

void add_history(uint32_t round, double pct, double cum_pct, uint64_t state_size, int n_threads, double round_time){
    // FORMAT: uint32_t round | double pct | double cum_pct| uint64_t map_size | int threads | double time
    FILE *history = fopen("history.txt", "a");
    fprintf(history, "%5d | %10.8f | %10.5f | %12d | %7i | %8.3f\n", round, pct, cum_pct, state_size, n_threads, round_time);
    printf("\033[8A");
    printf("\033[2K\r");
    printf("%5d | %10.8f | %10.5f | %12d | %7i | %8.3f\n", round, pct, cum_pct, state_size, n_threads, round_time);
    fclose(history);
}

void unify(khash_t(state_map) *current, int round, int threads, double time_elapsed){
    n_wins = 0;
    n_games = 0;
    for (int i = 0; i < num_threads; i++){
        n_wins += local_wins[i];
        n_games += local_games[i];
        local_wins[i] = 0;
        local_games[i] = 0;
        //uint64_t local_size = kh_size(local_next[i]);
        //printf("local map %i size: %llu\n", i, local_size);
        for (khiter_t k = kh_begin(local_next[i]);
            k != kh_end(local_next[i]);
            ++k)
        {
            if (!kh_exist(local_next[i], k))
                continue;
            State state = kh_key(local_next[i], k);
            __uint128_t multiplicity = kh_val(local_next[i], k);
            int ret;
            khiter_t key_next = kh_put(state_map, current, state, &ret);
            if (ret > 0){
                kh_value(current, key_next) = multiplicity;
            }
            else{
                kh_value(current, key_next) += multiplicity;
            }
        }
    }
    pct = ((double)n_wins / n_games) * 100;
    cum_pct += pct;
    add_history(round, pct, cum_pct, kh_size(current), threads, time_elapsed);
}

void save_state(khash_t(state_map) *state, uint32_t round, int n_threads){
    // FORMAT: uint32_t round | int num_threads | double cum_pct| uint64_t map_size | key_n, val_n
    remove("state.bin.tmp");
    FILE *bin_tmp = fopen("state.bin.tmp", "wb");
    uint64_t state_size = kh_size(state);
    fwrite(&round, sizeof(round), 1, bin_tmp);
    fwrite(&n_threads, sizeof(n_threads), 1, bin_tmp);
    fwrite(&cum_pct, sizeof(cum_pct), 1, bin_tmp);
    fwrite(&state_size, sizeof(state_size), 1, bin_tmp);
    for (khiter_t k = kh_begin(state); k < kh_end(state); k++){
        if (!kh_exist(state, k)){
            continue;
        }
        State key = kh_key(state, k);
        __uint128_t val = kh_val(state, k);
        fwrite(&key, sizeof(key), 1, bin_tmp);
        fwrite(&val, sizeof(val), 1, bin_tmp);
    }
    fclose(bin_tmp);
    remove("state.bin");
    rename("state.bin.tmp", "state.bin");
    remove("state.bin.tmp");
}

void load_state(khash_t(state_map) *state, uint32_t *round){
    // FORMAT: uint32_t round | float bytes_per_state | double cum_pct| uint64_t map_size | key_n, val_n
    FILE *bin_state = fopen("state.bin", "rb");
    if (bin_state == NULL){
        *round = 0;
        cum_pct = 0;
        num_threads = omp_get_max_threads();
        int ret;
        State initial_state = {.state = {0, 0, 0, 0}};
        khiter_t k = kh_put(state_map, state, initial_state, &ret);
        kh_value(state, k) = 1;
        return;
    }
    fread(round, sizeof(*round), 1, bin_state);
    printf("Loading Round %u\n", *round);
    fread(&num_threads, sizeof(num_threads), 1, bin_state);
    //printf("num_threads = %i\n", num_threads);
    fread(&cum_pct, sizeof(cum_pct), 1, bin_state);
    uint64_t state_size;
    fread(&state_size, sizeof(state_size), 1, bin_state);
    printf("Size %llu\n", state_size);
    State key;
    __uint128_t val;
    for (uint64_t i = 0; i < state_size; i++){
        fread(&key, sizeof(key), 1, bin_state);
        fread(&val, sizeof(val), 1, bin_state);
        int ret;
        khiter_t k = kh_put(state_map, state, key, &ret);
        kh_value(state, k) = val;
    }
    fclose(bin_state);
}

// TEMP HELPERS

static double gib(uint64_t bytes) {
    return bytes / (1024.0 * 1024.0 * 1024.0);
}

int set_threads(int round){
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);

    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    uint64_t commit = pmc.PrivateUsage;
    uint64_t predicted_states = 21 * round * round * round;
    const double safety = 0.90;
    uint64_t avail_ram = (uint64_t)((mem.ullAvailPhys + commit) * safety);
    uint64_t predicted_map_mem = (uint64_t)(predicted_states * bytes_per_state);
    uint64_t predicted_commit;
    int n_threads = omp_get_max_threads();
    while (n_threads > 1){
        predicted_commit = predicted_map_mem * (n_threads + 1);
        if (predicted_commit > avail_ram){
            n_threads--;
        }
        else{
            break;
        }
    }
    printf("----------------------------------------------------------------------\n");
    printf("\033[2K\r");
    printf("threads: %i\n", n_threads);
    printf("\033[2K\r");
    printf("bytes per state: %.1f B/s\n", bytes_per_state);
    printf("\033[2K\r");
    printf("Available RAM: %.2f GiB\n", gib((uint64_t)((mem.ullAvailPhys + commit) * safety)));
    printf("\033[2K\r");
    printf("predicted states: %llu\n", (unsigned long long)predicted_states);
    printf("\033[2K\r");
    printf("predicted map mem: %.2f GiB\n", gib(predicted_map_mem));
    printf("\033[2K\r");
    printf("predicted commit: %.2f GiB\n", gib(predicted_commit));
    printf("\033[2K\r");
    printf("real commit: %.2f GiB\n", gib(commit));
    return n_threads;
}

int main()
{
    omp_set_dynamic(0);
    int n_threads;
    int reset;
    printf("Reset? [0/1]: ");
    scanf("%i", &reset);
    if(reset){
        remove("state.bin");
        remove("history.txt");
        FILE *history = fopen("history.txt", "a");
        fprintf(history, "----------------------------------------------------------------------\n");
        fprintf(history, "%5s | %10s | %10s | %12s | %5s | %8s |\n", "Round", "Pct", "CumPct", "States", "Threads", "Time");
        fprintf(history, "----------------------------------------------------------------------\n");
        fclose(history);
    }
    num_threads = omp_get_max_threads();
    local_wins  = calloc(num_threads, sizeof(*local_wins));
    local_games = calloc(num_threads, sizeof(*local_games));
    local_next  = calloc(num_threads, sizeof(*local_next));
    for (int t = 0; t < num_threads; t++) {
        local_wins[t] = 0;
        local_games[t] = 0;
        local_next[t] = kh_init(state_map);
    }
    khash_t(state_map) *current = kh_init(state_map);
    uint32_t round;

    // initialize initial value

    load_state(current, &round);

    printf("Current Round: %i\n", round);

    initialize_transitions();

    printf("Depth: ");
    scanf("%i", &depth);

    FILE *history = fopen("history.txt", "r");

    if (history) {
        char buf[4096];

        while (fgets(buf, sizeof(buf), history))
            fputs(buf, stdout);

        fclose(history);
    }
    else{
        FILE *history = fopen("history.txt", "a");
        fprintf(history, "----------------------------------------------------------------------\n");
        fprintf(history, "%5s | %10s | %10s | %12s | %7s | %8s |\n", "Round", "Pct", "CumPct", "States", "Threads", "Time");
        fprintf(history, "----------------------------------------------------------------------\n");
        fclose(history);
        printf("----------------------------------------------------------------------\n");
        printf("%5s | %10s | %10s | %12s | %7s | %8s |\n", "Round", "Pct", "CumPct", "States", "Threads", "Time");
        printf("----------------------------------------------------------------------\n");
    }

    double t_start = omp_get_wtime();
    for (uint32_t i = round; i < depth; i++){
        if(i != round){
            n_threads = set_threads(i + 1);
            omp_set_num_threads(n_threads);
        }
        else{
            n_threads = num_threads;
            omp_set_num_threads(n_threads);
            printf("\n\n\n\n\n\n\n\n"); // gets deleted by diagnostic print in set_threads on next iteration
        }
        double t0 = omp_get_wtime();
        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            #pragma omp for schedule(dynamic, 1)
            for (khiter_t k = kh_begin(current); k < kh_end(current); k++){
                if (kh_exist(current, k)){
                    State state = kh_key(current, k);
                    __uint128_t multiplicity = kh_val(current, k);
                    step(state, multiplicity, tid);
                }
            }
        }
        PROCESS_MEMORY_COUNTERS_EX pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
        uint64_t commit = pmc.PrivateUsage;
        bytes_per_state = (float)((commit / (kh_size(current) * (n_threads + 1))));
        kh_clear(state_map, current);
        double t1 = omp_get_wtime();
        unify(current, i + 1, n_threads, t1 - t0);
        for (int t = 0; t < num_threads; t++){
            kh_clear(state_map, local_next[t]);
        }
        shift(current, n_games);
        save_state(current, i + 1, n_threads);
    }
    double t_end = omp_get_wtime();
    printf("Total Time: %.3f", t_end - t_start);
    return 0;
}
