#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "structs.h"
#include "khash.h"

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

static inline int state_equal(State a, State b)
{
    return memcmp(&a, &b, sizeof(State)) == 0;
}

static inline khint_t state_hash(State s)
{
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

void static inline shift(khash_t(state_map) *state, uint64_t n_games){
    while ((n_games >> 53) > 0){
        n_games >>= 1;
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

void step(khash_t(state_map) *current, khash_t(state_map) *next, uint16_t transitions[][4], int round){
    uint64_t n_wins = 0;
    uint64_t n_games = 0;
    double pct;
    for (khiter_t k = kh_begin(current);
        k != kh_end(current);
        ++k)
    {
        if (!kh_exist(current, k))
            continue;

        State state_current = kh_key(current, k);
        uint64_t multiplicity = kh_value(current, k);
        State state_next;
        for (int i = 0; i < n_tns; i++){               // saves sorting and normalizing
            state_next.state[0] = state_current.state[0] + trans_normalized_sorted[i][0];
            state_next.state[1] = state_current.state[1] + trans_normalized_sorted[i][1];
            state_next.state[2] = state_current.state[2] + trans_normalized_sorted[i][2];
            state_next.state[3] = state_current.state[3] + trans_normalized_sorted[i][3];

            if (is_won(state_next.state)){
                n_wins += multiplicity * trans_normalized_sorted[i][4];
            }
            else{
                int ret;
                khiter_t key_next = kh_put(state_map, next, state_next, &ret);
                if (ret > 0){
                    kh_value(next, key_next) = multiplicity * trans_normalized_sorted[i][4];
                }
                else{
                    kh_value(next, key_next) += multiplicity * trans_normalized_sorted[i][4];
                }
            }
            n_games += multiplicity * trans_normalized_sorted[i][4];
        }
        for (int i = 0; i < n_tn; i++){               // saves normalizing
            state_next.state[0] = state_current.state[0] + trans_normalized[i][0];
            state_next.state[1] = state_current.state[1] + trans_normalized[i][1];
            state_next.state[2] = state_current.state[2] + trans_normalized[i][2];
            state_next.state[3] = state_current.state[3] + trans_normalized[i][3];
            sort4(state_next.state);

            if (is_won(state_next.state)){
                n_wins += multiplicity * trans_normalized[i][4];
            }
            else{
                int ret;
                khiter_t key_next = kh_put(state_map, next, state_next, &ret);
                if (ret > 0){
                    kh_value(next, key_next) = multiplicity * trans_normalized[i][4];
                }
                else{
                    kh_value(next, key_next) += multiplicity * trans_normalized[i][4];
                }
            }
            n_games += multiplicity * trans_normalized[i][4];
        }
        for (int i = 0; i < n_t; i++){               // saves nothing lol
            state_next.state[0] = state_current.state[0] + trans[i][0];
            state_next.state[1] = state_current.state[1] + trans[i][1];
            state_next.state[2] = state_current.state[2] + trans[i][2];
            state_next.state[3] = state_current.state[3] + trans[i][3];
            normalize(state_next.state);
            sort4(state_next.state);

            if (is_won(state_next.state)){
                n_wins += multiplicity * trans[i][4];
            }
            else{
                int ret;
                khiter_t key_next = kh_put(state_map, next, state_next, &ret);
                if (ret > 0){
                    kh_value(next, key_next) = multiplicity * trans[i][4];
                }
                else{
                    kh_value(next, key_next) += multiplicity * trans[i][4];
                }
            }
            n_games += multiplicity * trans[i][4];
        }
    }
    shift(next, n_games);
    pct = ((float)n_wins / n_games) * 100;
    printf("Round: %i: %3f n_states: %u", round + 1, pct, kh_size(next));
}

int main()
{
    khash_t(state_map) *current = kh_init(state_map);
    khash_t(state_map) *next = kh_init(state_map);

    // initialize initial value

    int ret;

    State initial_state = {.state = {0, 0, 0, 0}};

    khiter_t k = kh_put(state_map, current, initial_state, &ret);

    kh_value(current, k) = 1;

    initialize_transitions();

    printf("Depth: ");
    scanf("%i", &depth);
    for (int i = 0; i < depth; i++){
        clock_t start = clock();
        step(current, next, transitions, i);
        clock_t stop = clock();
        printf(" round time: %.3f\n", (double)(stop - start) / CLOCKS_PER_SEC);
        khash_t(state_map) *tmp = current;
        current = next;
        next = tmp;
        kh_clear(state_map, next);
    }
    return 0;
}
