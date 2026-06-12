#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "structs.h"
#include "khash.h"

uint16_t transitions[1296][4];

uint8_t trans_normalized_sorted[56][5];
uint8_t trans_normalized[155][5];
uint8_t trans[460][5];
uint16_t n_tns = 0;
uint16_t n_tn = 0;
uint16_t n_t = 0;
int depth;

// init state map

KHASH_MAP_INIT_INT64(state_map, uint64_t)

khash_t(state_map) *current;
khash_t(state_map) *next;

// helper functions

static inline uint64_t pack_state(uint16_t s0, uint16_t s1, uint16_t s2, uint16_t s3){
    return (uint64_t)s0 << 48 | (uint64_t)s1 << 32 | (uint64_t)s2 << 16 | (uint64_t)s3;
}

static inline uint64_t normalize(uint64_t state){
    uint16_t s0 = state >> 48;
    uint16_t s1 = state >> 32;
    uint16_t s2 = state >> 16;
    uint16_t s3 = state;
    uint16_t min = (s0 < s1) ? s0 : s1;
    uint16_t min2 = (s2 < s3) ? s2 : s3;
    min = (min < min2) ? min : min2;
    s0 -= min;
    s1 -= min;
    s2 -= min;
    s3 -= min;
    return pack_state(s0, s1, s2, s3);
}

static inline void normalize_trans(uint16_t *trans){
    uint16_t min = (trans[0] < trans[1]) ? trans[0] : trans[1];
    uint16_t min2 = (trans[2] < trans[3]) ? trans[2] : trans[3];
    min = (min < min2) ? min : min2;
    trans[0] -= min;
    trans[1] -= min;
    trans[2] -= min;
    trans[3] -= min;
}

static inline uint64_t sort4(uint64_t state){          // sorts 4 element states, MUST be normalized
    uint16_t sorted[3] = {0, 0, 0};
    uint16_t buffer;
    uint8_t n_zeros = 0;
    for (uint8_t i = 0; i < 4; i++){
        buffer = state >> (16 * i);
        //printf("%i", buffer);
        if (buffer != 0){
            sorted[2 - i + n_zeros] = buffer;
        }
        else{
            n_zeros++;
        }
    }
    //printf("\n[%i, %i, %i, %i] -> ", 0, sorted[0], sorted[1], sorted[2]);
    if (n_zeros == 1){                                  // sorted[1:3] nonzero and unsorted
        //printf("n_zeros = %i -> ", n_zeros);
        if (sorted[1] < sorted[0]){
            buffer = sorted[0];
            sorted[0] = sorted[1];
            sorted[1] = buffer;
        }
        if (sorted[2] < sorted[0]){
            buffer = sorted[0];
            sorted[0] = sorted[2];
            sorted[2] = sorted[1];
            sorted[1] = buffer;
        }
        if (sorted[2] < sorted[1]){
            buffer = sorted[1];
            sorted[1] = sorted[2];
            sorted[2] = buffer;
        }
    }
    else if (n_zeros == 2){                                // sorted[2:3] nonzero and unsorted
        //printf("n_zeros = %i -> ", n_zeros);
        if (sorted[2] < sorted[1]){
            buffer = sorted[1];
            sorted[1] = sorted[2];
            sorted[2] = buffer;
        }
    }
    else{
        //printf("n_zeros = %i -> ", n_zeros);
    }
    //printf("[%i, %i, %i, %i]\n", 0, sorted[0], sorted[1], sorted[2]);
    return pack_state(0, sorted[0], sorted[1], sorted[2]);
}

static inline int is_won(uint64_t state){
    uint16_t s0 = state >> 48;
    uint16_t s1 = state >> 32;
    uint16_t s2 = state >> 16;
    uint16_t s3 = state;
    return s2 == 0 || (s1 == s3);
}

static inline void shift(khash_t(state_map) *state, uint64_t n_games){
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

void step(khash_t(state_map) *current, khash_t(state_map) *next, int round){
    uint64_t n_wins = 0;
    uint64_t n_games = 0;
    double pct;
    for (khiter_t k = kh_begin(current);
        k != kh_end(current);
        ++k)
    {
        if (!kh_exist(current, k))
            continue;

        uint64_t state_current = kh_key(current, k);
        uint64_t multiplicity = kh_value(current, k);
        uint64_t state_next;
        // state current values
        uint16_t sc_0 = state_current >> 48;
        uint16_t sc_1 = state_current >> 32;
        uint16_t sc_2 = state_current >> 16;
        uint16_t sc_3 = state_current;
        // state next values
        uint16_t sn_0;
        uint16_t sn_1;
        uint16_t sn_2;
        uint16_t sn_3;

        for (int i = 0; i < n_tns; i++){               // saves sorting and normalizing
            sn_0 = sc_0 + trans_normalized_sorted[i][0];
            sn_1 = sc_1 + trans_normalized_sorted[i][1];
            sn_2 = sc_2 + trans_normalized_sorted[i][2];
            sn_3 = sc_3 + trans_normalized_sorted[i][3];

            state_next = pack_state(sn_0, sn_1, sn_2, sn_3);

            if (is_won(state_next)){
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
            //printf("[%i, %i, %i, %i]\n", sn_0, sn_1, sn_2, sn_3);
        }
        for (int i = 0; i < n_tn; i++){               // saves normalizing
            sn_0 = sc_0 + trans_normalized[i][0];
            sn_1 = sc_1 + trans_normalized[i][1];
            sn_2 = sc_2 + trans_normalized[i][2];
            sn_3 = sc_3 + trans_normalized[i][3];

            state_next = pack_state(sn_0, sn_1, sn_2, sn_3);
            state_next = sort4(state_next);

            if (is_won(state_next)){
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
            sn_0 = sc_0 + trans[i][0];
            sn_1 = sc_1 + trans[i][1];
            sn_2 = sc_2 + trans[i][2];
            sn_3 = sc_3 + trans[i][3];

            state_next = pack_state(sn_0, sn_1, sn_2, sn_3);
            state_next = normalize(state_next);
            state_next = sort4(state_next);

            if (is_won(state_next)){
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
    printf("Round: %i: %3f%% n_states: %u", round + 1, pct, kh_size(next));
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
                    normalize_trans(transitions[m]);
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
    /*printf("trans_normalized_sorted\n");
    for (int i = 0; i < n_tns; i++){
        printf("[%i, %i, %i, %i] mult: %i\n", trans_normalized_sorted[n_tns][0], trans_normalized_sorted[i][1], trans_normalized_sorted[i][2], trans_normalized_sorted[i][3], trans_normalized_sorted[i][4]);
    }
    printf("trans_normalized\n");
    for (int i = 0; i < n_tn; i++){
        printf("[%i, %i, %i, %i] mult: %i\n", trans_normalized[i][0], trans_normalized[i][1], trans_normalized[i][2], trans_normalized[i][3], trans_normalized[i][4]);
    }
    printf("trans\n");
    for (int i = 0; i < n_t; i++){
        printf("[%i, %i, %i, %i] mult: %i\n", trans[i][0], trans[i][1], trans[i][2], trans[i][3], trans[i][4]);
    }*/
}

int main()
{
    khash_t(state_map) *current = kh_init(state_map);
    khash_t(state_map) *next = kh_init(state_map);

    // initialize initial value

    int ret;

    khiter_t k = kh_put(state_map, current, 0, &ret);

    kh_value(current, k) = 1;

    initialize_transitions();

    printf("Depth: ");
    scanf("%i", &depth);
    printf("\n");
    for (int i = 0; i < depth; i++){
        clock_t start = clock();
        step(current, next, i);
        clock_t stop = clock();
        printf(" round time: %.3fs\n", (double)(stop - start) / CLOCKS_PER_SEC);
        khash_t(state_map) *tmp = current;
        current = next;
        next = tmp;
        kh_clear(state_map, next);
    }
    return 0;
}
