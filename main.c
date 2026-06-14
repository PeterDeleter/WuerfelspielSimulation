#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include "structs.h"
#include "khash.h"

int num_threads;

uint16_t transitions[1296][4];

uint8_t trans_normalized_sorted[1296][5];
uint8_t trans_normalized[1296][5];
uint8_t trans[1296][5];
uint16_t n_tns = 0;
uint16_t n_tn = 0;
uint16_t n_ts = 0;
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
    uint64_t,       // value type
    1,              // map, not set
    state_hash,
    state_equal
)

khash_t(state_map) *current;
khash_t(state_map) *next;

uint64_t n_wins;
uint64_t n_games;

uint64_t *local_wins;
uint64_t *local_games;
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
    if (vector[2] == 0 || vector[1] == vector[3]){
        return 1;
    }
    return 0;
}

void static inline shift(khash_t(state_map) *state, uint64_t n_states){
    //printf("\nshifting\n%i\n", n_states);
    while ((n_states >> 53) > 0){
        n_states >>= 1;
        //printf("%i\n", n_states);
        for (khiter_t k = kh_begin(state);
            k != kh_end(state);
            ++k)
        {
            if (!kh_exist(state, k))
                continue;

            kh_value(state, k) >>= 1;
        }
    }
}

void step(State state_current, uint64_t multiplicity, int tid){
    State state_next;
    for (int i = 0; i < n_tns; i++){               // saves sorting and normalizing
        state_next.state[0] = state_current.state[0] + trans_normalized_sorted[i][0];
        state_next.state[1] = state_current.state[1] + trans_normalized_sorted[i][1];
        state_next.state[2] = state_current.state[2] + trans_normalized_sorted[i][2];
        state_next.state[3] = state_current.state[3] + trans_normalized_sorted[i][3];

        // todo: multiplicity * trans_normalized_sorted[i][4]; einmal berechnen

        if (is_won(state_next.state)){
            local_wins[tid] += multiplicity * trans_normalized_sorted[i][4];
        }
        else{
            int ret;
            khiter_t key_next = kh_put(state_map, local_next[tid], state_next, &ret);
            if (ret > 0){
                kh_value(local_next[tid], key_next) = multiplicity * trans_normalized_sorted[i][4];
            }
            else{
                kh_value(local_next[tid], key_next) += multiplicity * trans_normalized_sorted[i][4];
            }
        }
        local_games[tid] += multiplicity * trans_normalized_sorted[i][4];
    }
    for (int i = 0; i < n_tn; i++){               // saves normalizing
        state_next.state[0] = state_current.state[0] + trans_normalized[i][0];
        state_next.state[1] = state_current.state[1] + trans_normalized[i][1];
        state_next.state[2] = state_current.state[2] + trans_normalized[i][2];
        state_next.state[3] = state_current.state[3] + trans_normalized[i][3];
        sort4(state_next.state);

        if (is_won(state_next.state)){
            local_wins[tid] += multiplicity * trans_normalized[i][4];
        }
        else{
            int ret;
            khiter_t key_next = kh_put(state_map, local_next[tid], state_next, &ret);
            if (ret > 0){
                kh_value(local_next[tid], key_next) = multiplicity * trans_normalized[i][4];
            }
            else{
                kh_value(local_next[tid], key_next) += multiplicity * trans_normalized[i][4];
            }
        }
        local_games[tid] += multiplicity * trans_normalized[i][4];
    }
    for (int i = 0; i < n_t; i++){               // saves nothing lol
        state_next.state[0] = state_current.state[0] + trans[i][0];
        state_next.state[1] = state_current.state[1] + trans[i][1];
        state_next.state[2] = state_current.state[2] + trans[i][2];
        state_next.state[3] = state_current.state[3] + trans[i][3];
        normalize(state_next.state);
        sort4(state_next.state);

        if (is_won(state_next.state)){
            local_wins[tid] += multiplicity * trans[i][4];
        }
        else{
            int ret;
            khiter_t key_next = kh_put(state_map, local_next[tid], state_next, &ret);
            if (ret > 0){
                kh_value(local_next[tid], key_next) = multiplicity * trans[i][4];
            }
            else{
                kh_value(local_next[tid], key_next) += multiplicity * trans[i][4];
            }
        }
        local_games[tid] += multiplicity * trans[i][4];
    }
}

// post step functions

void unify(khash_t(state_map) *current){
    n_wins = 0;
    n_games = 0;
    for (int i = 0; i < num_threads; i++){
        n_wins += local_wins[i];
        n_games += local_games[i];
        local_wins[i] = 0;
        local_games[i] = 0;
        for (khiter_t k = kh_begin(local_next[i]);
            k != kh_end(local_next[i]);
            ++k)
        {
            if (!kh_exist(local_next[i], k))
                continue;
            State state = kh_key(local_next[i], k);
            uint64_t multiplicity = kh_val(local_next[i], k);
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
    float pct = ((float)n_wins / n_games) * 100;
    printf("%.5f%% n_states: %i ", pct, kh_size(current));
}

int main()
{
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

    // initialize initial value

    int ret;

    State initial_state = {.state = {0, 0, 0, 0}};

    khiter_t k = kh_put(state_map, current, initial_state, &ret);

    kh_value(current, k) = 1;

    initialize_transitions();

    printf("Depth: ");
    scanf("%i", &depth);
    double t_start = omp_get_wtime();
    for (int i = 0; i < depth; i++){
        printf("Round %i: ", i + 1);
        double t0 = omp_get_wtime();
        #pragma omp parallel
        {
            int tid = omp_get_thread_num();
            #pragma omp for schedule(dynamic, 1)
            for (khiter_t k = kh_begin(current); k < kh_end(current); k++){
                if (kh_exist(current, k)){
                    State state = kh_key(current, k);
                    uint64_t multiplicity = kh_val(current, k);
                    step(state, multiplicity, tid);
                }
            }
        }
        double t1 = omp_get_wtime();
        kh_clear(state_map, current);
        unify(current);
        double t2 = omp_get_wtime();
        for (int i = 0; i < num_threads; i++){
            kh_clear(state_map, local_next[i]);
        }
        shift(current, n_games);
        printf("times: %.3f step: %.3f unify; %.3f\n",t2 - t0, t1 - t0, t2 - t1);
    }
    double t_end = omp_get_wtime();
    printf("Total Time: %.3f", t_end - t_start);
    return 0;
}
