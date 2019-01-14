bool test_and_print_wrong_test = true;
bool manually_test = false;

#include "test_which.hpp"

// debug
#include <fstream>
#include "debug.hpp"

#include <boost/unordered_map.hpp>
#include <map>
#include <vector>

#include <stdint.h>
#include <sys/time.h>

#include <fstream>
#include <iostream>

using namespace std;

int main() {
    std::cout << "starto!\n";

    std::vector<std::pair<size_t, size_t>> configs;
    vector<size_t> elem_per_buck;
    vector<size_t> bucket_nums;

    // analyse by elem_per_bucket
    // associativity should be 4, 8 for alignment
    elem_per_buck.push_back(4);
    elem_per_buck.push_back(8);

    // analyse by bucket_num
    bucket_nums.push_back(5);
    bucket_nums.push_back(11);
    bucket_nums.push_back(31);
    bucket_nums.push_back(59);
    bucket_nums.push_back(71);
    bucket_nums.push_back(97);
    bucket_nums.push_back(101);

#ifdef TEST_YAGO
    string testing_dataset = "dataset/id_yago/cut_str_normal";
#else
    string testing_dataset = "dataset/str_normal";
#endif

    cout << "testing file: " << testing_dataset << endl;

    // configs: pair<bucket_num, elem_per_bucket>
    // stable bucket_num
    for (auto itt = elem_per_buck.begin(); itt != elem_per_buck.end(); itt++) {
        for (auto it = bucket_nums.begin(); it != bucket_nums.end(); it++) {
            configs.push_back(std::pair<size_t, size_t>(*itt, *it));
        }
    }

    fstream ff1("result", std::ios::out | std::ios::app);
    ff1 << testing_dataset << ":" << endl;

    //------------testing--------------
    std::string url;
    uint32_t v;
    uint64_t staUm;
    uint64_t endUm;
    uint64_t staTm;
    uint64_t endTm;

    std::fstream f(testing_dataset);
    boost::unordered_map<string, uint32_t> m1;
    boost::unordered_map<uint32_t, string> m2;

    staUm = get_time();
    uint64_t startUsedMemUm = getLftMem();

    uint32_t count = 0;
    while (f >> url >> v) {
        m1[url] = v;
        m2[v] = url;
        count++;
    }
    uint64_t endUsedMemUm = getLftMem();
    endUm = get_time();
    f.close();
    cout << "unordered_map use time: usec: \t\t\t" << endUm - staUm
         << std::endl;
    ff1 << "unordered_map_time: " << endUm - staUm << " "
        << "mem used: " << endUsedMemUm - startUsedMemUm << "\n";

    double virt = 0.0;
    double res = 0.0;
    for (auto it = configs.begin(); it != configs.end(); it++) {
        // ass is associativity
        size_t ass = it->first;
        size_t bn = it->second;

        staTm = get_time();

        myTrie::htrie_map<char, uint32_t> hm(ass, bn);
        std::fstream f1(testing_dataset);

        uint64_t startUsedMemTm = getLftMem();
        myTrie::debuging::clear_process_mem_usage();
        while (f1 >> url >> v) {
            hm.insertKV(url, v);
        }
        myTrie::debuging::process_mem_usage(virt, res);

        myTrie::debuging::clear_num();

        hm.shrink();
        cout << "shrinking cost time: " << shrink_total_time << endl;

        myTrie::debuging::print_tree_construct<char, uint32_t>(hm.t_root);
        myTrie::debuging::print_tree_construct_v2k<char, uint32_t>(hm.v2k);
        double mem_cal_inside = myTrie::debuging::print_res<char, uint32_t>();

        uint64_t endUsedMemTm = getLftMem();
        f1.close();

        endTm = get_time();

        cout << "finish trie_map constructing\n";
        cout << "constructing time: " << endTm - staTm << endl;
        cout << "expand cost time: " << expand_cost_time << endl;
        cout << "rehash cost time: " << rehash_cost_time << endl;

        int64_t hm_k_total_time = 0;
        int64_t um_k_total_time = 0;

        double max_percent_k = 0.0;
        double min_percent_k = 0.0;

        // checking:
        for (auto it = m1.begin(); it != m1.end(); it++) {
            std::string url1 = it->first;
            it++;
            std::string url2 = it->first;
            it++;
            std::string url3 = it->first;
            it++;
            std::string url4 = it->first;
            it++;
            std::string url5 = it->first;
            it++;
            if (it == m1.end()) {
                break;
            }

            uint32_t gotfromhm;
            int64_t hm_get_start = get_time();
            gotfromhm = hm.searchByKey(url1);
            gotfromhm = hm.searchByKey(url2);
            gotfromhm = hm.searchByKey(url3);
            gotfromhm = hm.searchByKey(url4);
            gotfromhm = hm.searchByKey(url5);
            int64_t hm_get_end = get_time();

            uint32_t gotfromum;
            int64_t um_get_start = get_time();
            gotfromum = m1[url1];
            gotfromum = m1[url2];
            gotfromum = m1[url3];
            gotfromum = m1[url4];
            gotfromum = m1[url5];
            int64_t um_get_end = get_time();

            int64_t um_used_time = um_get_end - um_get_start;
            int64_t hm_used_time = hm_get_end - hm_get_start;

            if (um_used_time == 0) {
                um_used_time = 1;
            }

            hm_k_total_time += hm_used_time;
            um_k_total_time += um_used_time;

            double cur_percent_k =
                (double)hm_used_time / (double)um_used_time - 1.0;

            if (max_percent_k < cur_percent_k) {
                max_percent_k = cur_percent_k;
            }
            if (min_percent_k > cur_percent_k) {
                min_percent_k = cur_percent_k;
            }
        }
        cout << "compare to unordered_map: accessing cost diff: "
             << hm_k_total_time / count << endl;

        // checking:
        int64_t hm_v_total_time = 0;
        int64_t um_v_total_time = 0;

        double max_percent_v = 0.0;
        double min_percent_v = 0.0;
        for (auto it = m2.begin(); it != m2.end(); it++) {
            uint32_t v1 = it->first;
            it++;
            uint32_t v2 = it->first;
            it++;
            uint32_t v3 = it->first;
            it++;
            uint32_t v4 = it->first;
            it++;
            uint32_t v5 = it->first;
            it++;
            if (it == m2.end()) {
                break;
            }

            int64_t hm_get_start = get_time();
            std::string gotfromhm = hm.searchByValue(v1);
            gotfromhm = hm.searchByValue(v2);
            gotfromhm = hm.searchByValue(v3);
            gotfromhm = hm.searchByValue(v4);
            gotfromhm = hm.searchByValue(v5);
            int64_t hm_get_end = get_time();

            int64_t um_get_start = get_time();
            std::string gotfromum = m2[v1];
            gotfromum = m2[v2];
            gotfromum = m2[v3];
            gotfromum = m2[v4];
            gotfromum = m2[v5];
            int64_t um_get_end = get_time();

            int64_t um_used_time = um_get_end - um_get_start;
            int64_t hm_used_time = hm_get_end - hm_get_start;

            if (um_used_time == 0) {
                um_used_time = 1;
            }

            hm_v_total_time += hm_used_time;
            um_v_total_time += um_used_time;

            double cur_percent_v =
                (double)hm_used_time / (double)um_used_time - 1.0;

            if (max_percent_v < cur_percent_v) {
                max_percent_v = cur_percent_v;
            }
            if (min_percent_v > cur_percent_v) {
                min_percent_v = cur_percent_v;
            }
        }
        cout << "compare to unordered_map: accessing cost diff: "
             << hm_v_total_time / count << endl;

#ifdef TEST_HAT
        cout << "unified_impl/1_tessil_hat_impl.hpp" << endl;

        ff1 << "hat_trie,,";
#endif
#ifdef TEST_HASH
        cout << "unified_impl/2_prototype_shrink.hpp" << endl;

        ff1 << "prototype,,";
#endif
#ifdef TEST_CUCKOOHASH
        cout << "unified_impl/3_prototype_cuckoo_shrink.hpp" << endl;

        ff1 << "cuckoo_hash,,";
#endif
#ifdef TEST_GROW_CUCKOOHASH_ASS
        cout << "unified_impl/4_prototype_cuckoo_grow_ass_shrink.hpp" << endl;

        ff1 << "grow_cuckoo_hash_ass,";

#ifdef REHASH_BEFORE_EXPAND
        ff1 << "rehash_before_expand,";
#else
        ff1 << "expand_before_rehash,";
#endif

#endif
#ifdef TEST_GROW_CUCKOOHASH_BUC
        cout << "unified_impl/5_prototype_cuckoo_grow_buc_shrink.hpp" << endl;

        ff1 << "grow_cuckoo_hash_buc,";

#ifdef REHASH_BEFORE_EXPAND
        ff1 << "rehash_before_expand,";
#else
        ff1 << "expand_before_rehash,";
#endif

#endif

        // config and memory
        ff1 << Associativity << "," << Bucket_num << ","
            << (endTm - staTm) / 1000 / (double)1000 << "," << virt << ","
            << res << "," << mem_cal_inside;

        // search by key
        ff1 << "," << hm_k_total_time << ","
            << (double)hm_k_total_time / (double)count << ","
            << (double)hm_k_total_time / (double)um_k_total_time * 100.0 << ","
            << max_percent_k * 100.0 << "," << min_percent_k * 100.0;

        // search by value
        ff1 << "," << hm_v_total_time << ","
            << (double)hm_v_total_time / (double)count << ","
            << (double)hm_v_total_time / (double)um_v_total_time * 100.0 << ","
            << max_percent_v * 100.0 << "," << min_percent_v * 100.0 << ",";

        // rehash counter and parent number
        ff1 << (double)rehash_total_num / (double)count << ","
            << ((double)myTrie::debuging::total_pass_trie_node_num /
                (double)count)
            << ",";

        // loading ratio
        ff1 << (double)myTrie::debuging::hashnode_total_element /
                   (double)myTrie::debuging::hashnode_total_slot_num *
                   (double)100
            << ","
            << (double)myTrie::debuging::hashnode_max_load /
                   (double)Max_slot_num * (double)100
            << ","
            << (double)myTrie::debuging::hashnode_min_load /
                   (double)Max_slot_num * (double)100;

        // expand and rehash cost time
        ff1 << "," << (double)expand_cost_time / (double)1000 / (double)1000
            << "," << (double)rehash_cost_time / (double)1000 / (double)1000;

        // node number
        ff1 << "," << myTrie::debuging::t_n << "," << myTrie::debuging::h_n
            << "," << myTrie::debuging::m_n;

        // page situation
        ff1 << ","
            << (double)myTrie::debuging::byte_used_in_page /
                   (double)myTrie::debuging::byte_pages_have * 100;
        ff1 << "," << myTrie::debuging::byte_pages_have / 1000 / 1000;
        ff1 << ","
            << (double)myTrie::debuging::total_page_number /
                   (double)myTrie::debuging::h_n
            << endl;

        ff1.flush();

        // correctness check
        if (test_and_print_wrong_test) {
            vector<string> wrong_search_key;
            vector<uint32_t> wrong_search_value;

            for (auto it = m1.begin(); it != m1.end(); it++) {
                if (it->second != hm.searchByKey(it->first)) {
                    wrong_search_key.push_back(it->first);
                }
            }

            cout << "test key finish!\n";
            cout << "wrong key_searching num: " << wrong_search_key.size()
                 << endl;

            for (auto it = m2.begin(); it != m2.end(); it++) {
                if (it->second != hm.searchByValue(it->first)) {
                    wrong_search_value.push_back(it->first);
                }
            }

            cout << "test value finish!\n";
            cout << "wrong value_searching num: " << wrong_search_value.size()
                 << endl;

            if (wrong_search_value.size() != 0 ||
                wrong_search_key.size() != 0) {
                cout << "testing failed\n";
                exit(0);
            }
        }

        if (manually_test) {
            while (cin >> url) {
                cout << "get value: " << hm.searchByKey(url) << endl;
            }
        }

        // clear status
        rehash_cost_time = 0;
        rehash_total_num = 0;

        fstream recal_time_file("recal_record", ios::out | ios::app);
        recal_time_file << testing_dataset << "," << Associativity << ","
                        << Bucket_num << ",";
        recal_time_file << recal_element_num_of_1st_char_counter;
        recal_time_file << "," << burst_total_counter << endl;
        recal_time_file.flush();

        recal_element_num_of_1st_char_counter = 0;
        burst_total_counter = 0;

        expand_cost_time = 0;
        rehash_cost_time = 0;
        rehash_total_num = 0;
        shrink_total_time = 0;
    }
    ff1 << endl;
    ff1.close();
}
