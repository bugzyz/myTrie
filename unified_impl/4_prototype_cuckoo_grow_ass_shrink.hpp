#include <stddef.h>
#include <climits>
#include <cstdint>

#include <cstdlib>
#include <vector>

#include <iostream>
#include <queue>
#include <sstream>
#include <stack>
#include "../util/hashFunc.hpp"
#include "../util/my_timer.hpp"

#include <fstream>
#include <ios>
#include <iostream>
#include <set>
#include <string>

#include <assert.h>

#define DEFAULT_Associativity 8
#define DEFAULT_Bucket_num 10
#define DEFAULT_Max_bytes_per_kv 4096
#define DEFAULT_SPECIAL_Max_bytes_per_kv_RATIO 4
#define DEFAULT_Burst_ratio 0.75

#define ALPHABET 256

#define DEFAULT_SPECIAL_Max_bytes_per_kv \
    DEFAULT_Max_bytes_per_kv* DEFAULT_SPECIAL_Max_bytes_per_kv_RATIO

#define NBITS_SPECIAL 1
#define NBITS_LEN 7   // 128 length
#define NBITS_POS 10  // 1024(4 align) pos
#define NBITS_PID 14  // 16384 page

#define NBITS_SPECIAL_S 1
#define NBITS_LEN_S 13  // 8192 length
#define NBITS_POS_S 9   // 512(32 align) pos
#define NBITS_PID_S 9   // 512 page

#define MAX_NORMAL_PAGE (1 << NBITS_PID)
#define MAX_SPECIAL_PAGE (1 << NBITS_PID_S)

#define MAX_NORMAL_LEN (1 << NBITS_LEN)

// #define NBITS_SPECIAL 1
// #define NBITS_LEN 7   // 128 length
// #define NBITS_POS 28  // 268435456(1 align) pos
// #define NBITS_PID 28  // 268435456 page

// #define NBITS_SPECIAL_S 1
// #define NBITS_LEN_S 13  // 8192 length
// #define NBITS_POS_S 25   // 33554432(32 align) pos
// #define NBITS_PID_S 25   // 33554432 page


// #define DEFAULT_SPECIAL_ALIGNMENT \
//     DEFAULT_SPECIAL_Max_bytes_per_kv / (1 << NBITS_POS_S)
#define DEFAULT_SPECIAL_ALIGNMENT 32
#define DEFAULT_NORMAL_ALIGNMENT 4

static uint32_t longest_string_size;

uint32_t recal_element_num_of_1st_char_counter = 0;
uint32_t burst_total_counter = 0;

uint64_t burst_total_time = 0;
uint64_t cal_prefix_total_time = 0;
uint64_t write_page_total_time = 0;

// todo: wait to be deleted, just for recording the time that expand() cost
uint64_t expand_cost_time = 0;
uint64_t rehash_cost_time = 0;
uint64_t rehash_total_num = 0;

uint64_t shrink_total_time = 0;
uint64_t clean_prefix_total_time = 0;

// configuration
// static size_t Associativity;
// static size_t Bucket_num;
// static size_t Max_bytes_per_kv;
// static double Burst_ratio;
// static size_t Max_slot_num;
// static size_t Max_loop;

size_t Associativity;
size_t Bucket_num;
size_t Max_bytes_per_kv;
double Burst_ratio;
size_t Max_slot_num;
size_t Max_loop;

using namespace std;

namespace myTrie {
// charT = char, T = value type, keysizeT = the type describe keysize
template <class CharT, class T, class KeySizeT = std::uint16_t>
class htrie_map {
   public:
    // DEFAULT_Associativity * DEFAULT_Bucket_num should be set to be greater
    // than 26 for 26 alaphbet and ", @ .etc.(the test in lubm40 shows that it
    // has 50 char species)
    enum class node_type : unsigned char {
        HASH_NODE,
        TRIE_NODE,
        MULTI_NODE,
    };

    class trie_node;
    class hash_node;
    class MultiTrieNode;
    class iterator;
    class page;
    class slot;

    class page_group;

    class anode {
       public:
        node_type _node_type;
        trie_node* parent;

        bool is_hash_node() { return _node_type == node_type::HASH_NODE; }
        bool is_trie_node() { return _node_type == node_type::TRIE_NODE; }
        bool is_multi_node() { return _node_type == node_type::MULTI_NODE; }

        void delete_me() {
            if (this->is_trie_node()) {
                std::map<CharT, trie_node*> childs =
                    ((trie_node*)this)->trie_node_childs;
                for (auto it = childs.begin(); it != childs.end(); it++) {
                    it->second->delete_me();
                    delete it->second;
                }
            } else {
                delete (hash_node*)this;
            }
        }
    };

    /*-----------------double array
     * string_child_representation-----------------*/
    class string_child_representation {
        class child_node {
           public:
            int hash_val;
            string child_string;
            anode* child;

            child_node() : hash_val(0), child_string(""), child(nullptr) {}

            child_node(int hash_val, string cs, anode* n)
                : hash_val(hash_val), child_string(cs), child(n) {}

            void operator=(const child_node& cn) {
                hash_val = cn.hash_val;
                child_string = cn.child_string;
                child = cn.child;
            }
        };

       public:
        class child_iterator {
           public:
            string first;
            anode* second;

            child_iterator(string f, anode* tn) : first(f), second(tn) {}

            inline child_iterator* operator->() { return this; }

            inline bool operator==(child_iterator& right) {
                return second == right.second;
            }

            inline bool operator==(const child_iterator& right) {
                return second == right.second;
            }

            inline bool operator!=(const child_iterator& right) {
                return second != right.second;
            }
        };

       private:
        child_node* child_family;
        int size_;

       public:
        string_child_representation(size_t child_number)
            : size_(child_number) {
            child_family = new child_node[child_number];
        }

        inline void add_child(string child_string, size_t child_index,
                              anode* n) {
            child_family[child_index].child_string = child_string;
            child_family[child_index].child = n;
            child_family[child_index].hash_val = myTrie::hashRelative::hash(
                child_string.data(), child_string.size());
        }

        inline child_iterator find(string cs) {
            // binary search
            if (size_ == 1 && cs == child_family[0].child_string)
                return child_iterator(cs, child_family[0].child);
            int target_hash = myTrie::hashRelative::hash(cs.data(), cs.size());
            int low = 0;
            int high = size_ - 1;

            int target_index = -1;
            while (low <= high) {
                int mid = low + (high - low) / 2;
                if (child_family[mid].hash_val == target_hash) {
                    target_index = mid;
                    break;
                } else if (child_family[mid].hash_val > target_hash)
                    high = mid - 1;
                else
                    low = mid + 1;
            }

            for (int i = target_index; target_index != size_ &&
                                       child_family[i].hash_val == target_hash;
                 i++) {
                if (child_family[i].child_string == cs) {
                    return child_iterator(cs, child_family[i].child);
                }
            }
            return child_iterator(cs, nullptr);

            // // ordered find
            // for (int i = 0; i != size_; i++) {
            //     if (cs == child_family[i].child_string) {
            //         return child_iterator(cs, child_family[i].child);
            //     }
            // }

            // return child_iterator(cs, nullptr);
        }

        inline size_t size() { return size_; }

        inline child_iterator end() { return child_iterator("", nullptr); }
    };

    class multi_node : public anode {
       public:
        string_child_representation childs_;
        size_t string_keysize_;

        multi_node(size_t _string_keysize_, size_t child_number)
            : string_keysize_(_string_keysize_),childs_(string_child_representation(child_number)) {
            anode::_node_type = node_type::MULTI_NODE;
            anode::parent = nullptr;
        }

        anode* find_child(const CharT* key, size_t key_size) {
            auto it = childs_.find(string(key, string_keysize_));
            if (it == childs_.end()) {
                return nullptr;
            } else {
                return it->second;
            }
        }

        void add_child(string child_string, size_t child_index, anode* n){
            childs_.add_child(child_string, child_index, n);
        }
    };

#ifdef ARRAY_REP
    /*-----------------array child_representation-----------------*/
    class child_representation {
        trie_node** childs;
        int number;

       public:
        class child_iterator {
           public:
            char first;
            trie_node* second;

            child_iterator(char c, trie_node* tn) : first(c), second(tn) {}

            inline child_iterator* operator->() { return this; }

            inline bool operator==(child_iterator& right) {
                return second == right.second;
            }

            inline bool operator!=(const child_iterator& right) {
                return second != right.second;
            }
        };

        child_representation() : number(0) {
            childs = (trie_node**)malloc(sizeof(trie_node*) * ALPHABET);
            for (int i = 0; i != ALPHABET; i++) {
                childs[i] = nullptr;
            }
        }

        inline trie_node*& operator[](char c) {
            number++;
            return childs[(int)c];
        }

        inline child_iterator find(char c) {
            return child_iterator(c, childs[(int)c]);
        }

        inline size_t size() { return number; }

        inline child_iterator end() { return child_iterator(0, nullptr); }

        inline child_iterator begin() {
            if (number != 0) {
                for (int i = 0; i != ALPHABET; i++) {
                    if (childs[i]) return child_iterator((char)i, childs[i]);
                }
            }
            return child_iterator(0, nullptr);
        }

        inline void get_childs(vector<anode*>& res) {
            for (int i = 0; i != ALPHABET; i++) {
                if (childs[i]) res.push_back(childs[i]);
            }
        }

        inline void get_childs_with_char(vector<pair<CharT, trie_node*>>& res) {
            for (int i = 0; i != ALPHABET; i++)
                if (childs[i])
                    res.push_back(pair<CharT, trie_node*>((char)i, childs[i]));
        }

        size_t get_childs_representation_mem() {
            return sizeof(child_representation) + ALPHABET * sizeof(trie_node*);
        }
    };
#endif

#ifdef LIST_REP
    /*-----------------list child_representation-----------------*/
    class child_representation {
        class child_node {
           public:
            char child_node_char;
            trie_node* current;
            child_node* next;

            child_node(char cnc, trie_node* cur)
                : child_node_char(cnc), current(cur), next(nullptr) {}

            child_node()
                : child_node_char(0), current(nullptr), next(nullptr) {}

            inline bool have_next() { return next != nullptr; }

            inline child_node* next_child() { return next; }

            inline void add_next_child(char c) {
                next = new child_node(c, new trie_node(nullptr));
            }
        };

        class child_iterator {
           public:
            char first;
            trie_node* second;

            child_iterator(char c, trie_node* tn) : first(c), second(tn) {}

            inline child_iterator* operator->() { return this; }

            inline bool operator==(child_iterator& right) {
                return second == right.second;
            }

            inline bool operator!=(const child_iterator& right) {
                return second != right.second;
            }
        };

        child_node* first_child;
        int number;

       public:
        child_representation() : number(0), first_child(nullptr) {}

        inline trie_node*& operator[](char c) {
            //find the ok node
            child_node* current_child_node = first_child;
            child_node* last_child_node = nullptr;

            while (current_child_node) {
                if (current_child_node->child_node_char == c)
                    return current_child_node->current;

                last_child_node = current_child_node;
                current_child_node = current_child_node->next;
            }

            if (first_child == nullptr) {
                first_child = new child_node(c, nullptr);

                number++;
                return first_child->current;
            }

            last_child_node->add_next_child(c);
            number++;
            return (last_child_node->next_child())->current;
        }

        inline child_iterator find(char c) {
            child_node* current_child_node = first_child;
            do {
                if (current_child_node->child_node_char == c)
                    return child_iterator(c, current_child_node->current);
            } while (((current_child_node = current_child_node->next_child()) !=
                      nullptr));
            return child_iterator(c, nullptr);
        }

        inline size_t size() { return number; }

        inline child_iterator begin() {
            return first_child != nullptr
                       ? child_iterator(first_child->child_node_char,
                                        first_child->current)
                       : child_iterator(0, nullptr);
        }

        inline child_iterator end() { return child_iterator(0, nullptr); }

        inline void get_childs(vector<anode*>& res) {
            child_node* current_child_node = first_child;

            while (current_child_node) {

                res.push_back(current_child_node->current);

                current_child_node = current_child_node->next;
            }
        }

        inline void get_childs_with_char(vector<pair<CharT, trie_node*>>& res) {
            child_node* current_child_node = first_child;

            while (current_child_node) {
                res.push_back(
                    pair<CharT, trie_node*>(current_child_node->child_node_char,
                                            current_child_node->current));

                current_child_node = current_child_node->next;
            }
        }

        size_t get_childs_representation_mem() {
            return sizeof(child_representation) + number * sizeof(child_node);
        }

        void delete_child_representation(){
            child_node* current_child_node = first_child;
            child_node* temp_child_node = nullptr;

            while (current_child_node) {
                temp_child_node = current_child_node;
                current_child_node = current_child_node->next;
                free(temp_child_node);
                // cout << "freeing child_representation!" << endl;
            }
        }
    };
#endif

#ifdef MAP_REP
    /*-----------------map child_representation-----------------*/
    class child_representation {

       public:
        map<char, trie_node*> childs;
        child_representation() {}

        inline trie_node*& operator[](char c) { return childs[(int)c]; }

        inline typename map<char, trie_node*>::iterator find(char c) {
            return childs.find(c);
        }

        inline size_t size() { return childs.size(); }

        inline typename map<char, trie_node*>::iterator end() {
            return childs.end();
        }

        inline typename map<char, trie_node*>::iterator begin() {
            return childs.begin();
        }

        void get_childs(vector<anode*> &res) {
            for (auto it = childs.begin(); it != childs.end(); it++) {
                res.push_back(it->second);
            }
        }

        inline void get_childs_with_char(vector<pair<CharT, trie_node*>>& res) {
            for(auto it = childs.begin();it!=childs.end();it++){
                res.push_back(pair<CharT,trie_node*>(it->first, it->second));
            }
        }
    };
#endif

    class trie_node : public anode {
       public:
        // store the suffix of hash_node or trie_node
        child_representation trie_node_childs;
        // std::map<CharT, trie_node*> trie_node_childs;
        hash_node* hash_node_child;

        // prefix
        CharT* prefix;
        uint16_t prefix_len;

        // element end up here
        bool have_value;
        T value;

        trie_node(trie_node* p)
            : have_value(false),
              value(T()),
              hash_node_child(nullptr),
              prefix(nullptr),
              prefix_len(0) {
            anode::_node_type = node_type::TRIE_NODE;
            anode::parent = p;
        }

        iterator search_kv_in_trienode() {
            return iterator(have_value, value, this, 0, 0);
        }

        std::pair<bool, T> insert_kv_in_trienode(const CharT* key,
                                                 size_t key_size,
                                                 htrie_map<CharT, T>* hm, T v) {
            set_prefix(key, key_size);
            have_value = true;
            value = v;
            hm->set_v2k(v, this, -1);
            return pair<bool,T>(true, v);
        }

        /*-------*/
        void get_childs_vector(vector<anode*> &res) {
            //get all trie_node childs
            trie_node_childs.get_childs(res);
            
            //if no trie_node childs, it must have a hash_node child
            if (res.size()==0)
                res.push_back(hash_node_child);
        }

        /*-------------------prefix relative-------------------*/
        size_t get_prefix(char* buf) {
            memcpy(buf, prefix, prefix_len);
            return prefix_len;
        }

        inline string get_prefix() {
            return prefix == nullptr ? string() : string(prefix, prefix_len);
        }

        void set_prefix(const CharT* key, size_t key_size) {
            if (key_size != 0) {
                char* cur_prefix = (char*)malloc(key_size);
                memcpy(cur_prefix, key, key_size);
                free(prefix);
                prefix = cur_prefix;
                prefix_len = key_size;
            } else {
                if (prefix != nullptr) free(prefix);
                prefix = nullptr;
                prefix_len = 0;
            }
        }

        void set_prefix(std::string& p) {
            uint16_t len = p.size();
            if (len != 0) {
                char* cur_prefix = (char*)malloc(p.size());
                memcpy(cur_prefix, p.data(), len);
                free(prefix);
                prefix = cur_prefix;
                prefix_len = len;
            } else {
                if (prefix != nullptr) free(prefix);
                prefix = nullptr;
                prefix_len = 0;
            }
        }

        void clean_prefix(){
            if (prefix != nullptr) free(prefix);
        }

        ~trie_node() {
            // if (prefix != nullptr) free(prefix);
            // delete trie_node_childs;
            trie_node_childs.delete_child_representation();
        }

        // finding target, if target doesn't exist, return nullptr
        trie_node* find_trie_node_child(CharT c) {
            auto found = trie_node_childs.find(c);
            if (found != trie_node_childs.end()) {
                trie_node* target = found->second;
                return target;
            } else {
                return nullptr;
            }
        }

        // finding target, if target doesn't exist, create new trie_node with
        // hash_node son and return new trie_node
        trie_node* find_trie_node_child(CharT c, const CharT* key,
                                        size_t key_size,
                                        htrie_map<CharT, T>* hm) {
            auto found = trie_node_childs.find(c);
            if (found != trie_node_childs.end()) {
                trie_node* target = found->second;
                return target;
            } else {
                trie_node* son_trie_node = new trie_node(this);
                this->add_trie_node_child(son_trie_node, c);
                son_trie_node->set_hash_node_child(new hash_node(
                    son_trie_node, string(key, key_size) + c, hm));
                return son_trie_node;
            }
        }

        inline void set_hash_node_child(hash_node* node) {
            hash_node_child = node;
        }

        inline hash_node* get_hash_node_child() { return hash_node_child; }

        void add_trie_node_child(trie_node* node, CharT c) {
            trie_node_childs[c] = node;
            // clear the hash_node_child because a trie_node will only have
            // trie_node_childs map or have a single hash_node_child
            hash_node_child = nullptr;
        }

        pair<CharT, trie_node*> get_only_trie_node_child() {
            if (trie_node_childs.size() == 1) {
                auto it = trie_node_childs.begin();
                return pair<CharT, trie_node*>(it->first, it->second);
            }
            return pair<CharT, trie_node*>(CharT(), nullptr);
        }

        hash_node* get_only_hash_node_child() { return hash_node_child; }
    };

    class hash_node : public anode {
       public:
        slot* key_metas;
        size_t elem_num;

        size_t cur_associativity = 1;

        int common_prefix_len;

        slot first_slot;

        // normal page_group id
        uint8_t normal_pgid;
        // special page_group id
        uint8_t special_pgid;

       public:
        // debug function
        void print_slot(int i, int j, htrie_map<CharT, T>* hm) {
            slot* s = key_metas + i * cur_associativity + j;
            cout << i * cur_associativity + j << ":" << s->get_special() << ","
                 << s->get_length() << "," << s->get_pos() << ","
                 << s->get_page_id() << "," ;
            string str = string(hm->get_tail_pointer(s), s->get_length());
            cout << str;
            T v = hm->get_tail_v(s);
            cout << "=" << v << "\n";
        }

        static void print_slot(slot* s) {
            cout << s->get_special() << "," << s->get_length() << ","
                 << s->get_pos() << "," << s->get_page_id() << "," << endl;
        }

        void print_key_metas(htrie_map<CharT, T>* hm) {
            for (int i = 0; i != Bucket_num; i++) {
                for (int j = 0; j != cur_associativity; j++) {
                    print_slot(i, j, hm);
                }
                cout << "---\n";
            }
        }

       public:
        explicit hash_node(trie_node* p, string prefix, htrie_map<CharT, T>* hm,
                           size_t need_associativity = 1)
            : cur_associativity(need_associativity > Associativity
                                    ? Associativity
                                    : need_associativity),
              elem_num(0),
              common_prefix_len(INT_MAX),
              first_slot(0, 0, 0, 0),
              normal_pgid(hm->get_normal_group_id()),
              special_pgid(hm->get_special_group_id()) {
            anode::_node_type = node_type::HASH_NODE;
            anode::parent = p;
            if (p != nullptr) anode::parent->set_prefix(prefix);

            key_metas =
                (slot*)malloc(cur_associativity * Bucket_num * sizeof(slot));
            // key_metas = new slot[cur_associativity * Bucket_num * sizeof(slot)];

            // init key space
            for (int i = 0; i != cur_associativity * Bucket_num; i++) {
                key_metas[i].set_slot(slot());
            }
        }

        ~hash_node() { free(key_metas); }

        string get_prefix() {
            return anode::parent != nullptr ? anode::parent->get_prefix()
                                            : string();
        }

        inline slot* get_slot(size_t bucketid, size_t slotid) {
            return key_metas + bucketid * cur_associativity + slotid;
        }

        inline slot* get_slot(int index) { return key_metas + index; }

        inline int get_index(slot* s) { return s - key_metas; }

        void get_all_elements(htrie_map<CharT, T>* hm,
                              std::map<std::string, T>& elements) {
            for (size_t i = 0; i != Bucket_num; i++) {
                for (size_t j = 0; j != cur_associativity; j++) {
                    slot& cur_slot = key_metas[i * cur_associativity + j];
                    if (cur_slot.isEmpty()) break;
                    hm->get_tail_str_v(get_page_group_id(&cur_slot), elements,
                                       &cur_slot);
                }
            }
        }

        /*---------function that changed key_metas layout---------*/

        /*------------------ 0. helper function------------------*/

        void apply_the_changed_searchPoint(map<T, int>& searchPoints,
                                           htrie_map<CharT, T>* hm) {
            for (auto it = searchPoints.begin(); it != searchPoints.end(); it++)
                hm->set_searchPoint_index(it->first, it->second);
        }

        /*------------------ 1. expand function------------------*/

        bool expand_key_metas_space(size_t need_associativity, htrie_map* hm) {
            uint64_t sta = get_time();
            // we cannot expand anymore, return false
            if (cur_associativity == Associativity) {
                return false;
            }

            if (need_associativity > Associativity) {
                need_associativity = Associativity;
            }

            map<T, int> updating_search_points;
            slot* new_key_metas =
                (slot*)malloc(need_associativity * Bucket_num * sizeof(slot));
            for (int i = 0; i != Bucket_num; i++) {
                for (int j = 0; j != need_associativity; j++) {
                    slot* cur_new_slot =
                        new_key_metas + i * need_associativity + j;
                    if (j < cur_associativity) {
                        slot* cur_slot = key_metas + i * cur_associativity + j;
                        cur_new_slot->set_slot(cur_slot);
                        // if cur_slot is not empty which means we need to
                        // update its slot position in v2k
                        // adding the new position to the searchPoint update
                        // list
                        if (!cur_slot->isEmpty()) {
                            T v = hm->get_tail_v(get_page_group_id(cur_slot),
                                                 cur_slot);
                            updating_search_points[v] =
                                i * need_associativity + j;
                        }
                    } else {
                        cur_new_slot->set_slot(0, 0, 0, 0);
                    }
                }
            }
            // applying the updating searchPoint
            apply_the_changed_searchPoint(updating_search_points, hm);

            // switch the old key_metas to the new key_metas and release the old
            // key_metas
            free(key_metas);
            key_metas = new_key_metas;

            cur_associativity = need_associativity;
            uint64_t end = get_time();

            expand_cost_time += end - sta;

            return true;
        }

        /*------------------ 2. rehashing function------------------*/

        inline slot* previous_dst_slot_in_same_bucket(slot* s) {
            size_t slotid = (s - key_metas) % cur_associativity;
            if (slotid == 0) {
                return nullptr;
            } else
                return s - 1;
        }

        // return another possible bucketid that the slot *s can be
        inline size_t get_another_bucketid(htrie_map<CharT, T>* hm, slot* s,
                                           size_t current_bucketid) {
            size_t bucketid1 =
                myTrie::hashRelative::hash(
                    hm->get_tail_pointer(get_page_group_id(s), s),
                                           s->get_length(), 1) %
                Bucket_num;
            size_t bucketid2 =
                myTrie::hashRelative::hash(
                    hm->get_tail_pointer(get_page_group_id(s), s),
                                           s->get_length(), 2) %
                Bucket_num;
            return current_bucketid == bucketid1 ? bucketid2 : bucketid1;
        }

        int cuckoo_hash(size_t bucketid, htrie_map<CharT, T>* hm) {
            rehash_total_num++;
            uint64_t sta = get_time();
            // bucket_list records the mapping of bucket_id=last_empty_slot_id
            std::map<size_t, size_t> bucket_list;
            for (size_t bn = 0; bn != Bucket_num; bn++) {
                bucket_list[bn] = cur_associativity;
                for (int sn = 0; sn != cur_associativity; sn++) {
                    if (key_metas[bn * cur_associativity + sn].isEmpty()) {
                        bucket_list[bn] = sn;
                        break;
                    }
                }
            }
            // current bucket is definitely full
            // just pick the last slot to kick
            int ret_slot_id = cur_associativity - 1;

            size_t kicked_slot_id = -1;
            for (int i = 0; i != cur_associativity; i++) {
                slot* s = get_slot(bucketid, i);
                size_t bkid = get_another_bucketid(hm, s, bucketid);
                if (bkid != bucketid) {
                    kicked_slot_id = i;
                }
            }
            if (kicked_slot_id == -1) {
                uint64_t end = get_time();
                rehash_cost_time += end - sta;

                return -1;
            }
            // set up the backup for recovery if the rehash fails
            char* key_metas_backup =
                (char*)malloc(Bucket_num * cur_associativity * sizeof(slot));
            memcpy(key_metas_backup, key_metas,
                   Bucket_num * cur_associativity * sizeof(slot));

            ret_slot_id = kicked_slot_id;

            size_t current_bucket_id = bucketid;

            slot src_slot = slot(0, 0, 0, 0);
            slot* dst_slot = get_slot(current_bucket_id, kicked_slot_id);

            size_t rehash_count = 0;

            size_t last_current_bucketid = 0;
            size_t last_bucketid_kick_to = 0;

            map<T, int> searchPoint_wait_2_be_update;
            do {
                /*
                    src(a,b,c)
                    cur_bucket: |x      |x      |x      |dst(d,e,f)|
                    kk2_bucket: |x      |x      |x      |x         |
                */
                // calculate the destination
                size_t bucketid_kick_to =
                    get_another_bucketid(hm, dst_slot, current_bucket_id);

                // if the slot can only place in one bucket, we change the
                // dst_slot
                // if the cuckoo hash kick as a circle, we change the dst_slot
                // to try to break the circle
                if (bucketid_kick_to == current_bucket_id ||
                    (last_bucketid_kick_to == current_bucket_id &&
                     last_current_bucketid == bucketid_kick_to)) {
                    dst_slot = previous_dst_slot_in_same_bucket(dst_slot);
                    rehash_count++;
                    if (dst_slot == nullptr) {
                        // recover the key_metas
                        memcpy(key_metas, key_metas_backup,
                               Bucket_num * cur_associativity * sizeof(slot));
                        free(key_metas_backup);
                        uint64_t end = get_time();
                        rehash_cost_time += end - sta;
                        return -1;
                    }
                    continue;
                }

                /*
                    src(a,b,c)
                    temp: d,e,f
                    cur_bucket: |x      |x      |x      |dst(d,e,f)|
                    kk2_bucket: |x      |x      |x      |x         |
                */
                bool temp_special = dst_slot->get_special();
                KeySizeT temp_length = dst_slot->get_length();
                size_t temp_pos = dst_slot->get_pos();
                size_t temp_page_id = dst_slot->get_page_id();

                // if dst_slot is empty, it means now the dst_slot is the first
                // place we clear for the target slot
                if (dst_slot->isEmpty()) {
                    // recover the key_metas
                    memcpy(key_metas, key_metas_backup,
                           Bucket_num * cur_associativity * sizeof(slot));
                    free(key_metas_backup);
                    uint64_t end = get_time();
                    rehash_cost_time += end - sta;
                    return -1;
                }

                /*
                    src(a,b,c)
                    temp: d,e,f
                    cur_bucket: |x      |x      |x      |dst(a,b,c)|
                    kk2_bucket: |x      |x      |x      |x         |
                */
                dst_slot->set_slot(src_slot);
                if (!dst_slot->isEmpty())
                    searchPoint_wait_2_be_update[hm->get_tail_v(
                        get_page_group_id(dst_slot), dst_slot)] =
                        get_index(dst_slot);

                // if the destination bucket isn't full, just fill the empty
                // slot and return
                if (bucket_list[bucketid_kick_to] != cur_associativity) {
                    /* kick2bucket is have empty slot
                        src(a,b,c)
                        temp: d,e,f
                        cur_bucket: |x      |x      |x      |dst(a,b,c)|
                        kk2_bucket: |x      |x      |x      |0         |
                    */
                    // update dst_slot to the dest bucket's first empty slot
                    /*
                        src(a,b,c)
                        temp: d,e,f
                        cur_bucket: |x      |x      |x      |x(a,b,c)  |
                        kk2_bucket: |x      |x      |x      |0-dst     |
                    */
                    dst_slot = get_slot(bucketid_kick_to,
                                        bucket_list[bucketid_kick_to]);

                    /*
                        src(a,b,c)
                        temp: d,e,f
                        cur_bucket: |x      |x      |x      |x(a,b,c)  |
                        kk2_bucket: |x      |x      |x      |dst(d,e,f)|
                    */
                    dst_slot->set_slot(temp_special, temp_length, temp_pos,
                                       temp_page_id);
                    searchPoint_wait_2_be_update[hm->get_tail_v(
                        get_page_group_id(dst_slot), dst_slot)] =
                        get_index(dst_slot);
                    apply_the_changed_searchPoint(searchPoint_wait_2_be_update,
                                                  hm);
                    free(key_metas_backup);
                    uint64_t end = get_time();
                    rehash_cost_time += end - sta;
                    return ret_slot_id;
                }

                /* kick2bucket is full
                    src(a,b,c)
                    temp: d,e,f
                    cur_bucket: |x      |x      |x      |dst(a,b,c)|
                    kk2_bucket: |x      |x      |x      |x         |
                */

                /* kick2bucket is full
                    src(a,b,c)
                    temp: d,e,f
                    cur_bucket: |x      |x      |x      |x(a,b,c)  |
                    kk2_bucket: |x      |x      |x      |x-dst     |
                */
                // update dst_slot to the dest bucket's last empty slot
                dst_slot = get_slot(bucketid_kick_to, cur_associativity - 1);
                /*
                     src(d,e,f)
                     cur_bucket: |x      |x      |x      |x(a,b,c)  |
                     kk2_bucket: |x      |x      |x      |x-dst     |
                */
                src_slot.set_slot(temp_special, temp_length, temp_pos,
                                  temp_page_id);
                /*
                     src(d,e,f)
                     (kk2_bucket)
                     cur_bucket: |x      |x      |x      |x-dst     |
                */
                last_bucketid_kick_to = bucketid_kick_to;
                last_current_bucketid = current_bucket_id;
                current_bucket_id = bucketid_kick_to;

                rehash_count++;
            } while (rehash_count != Max_loop);
            // recover the key_metas
            memcpy(key_metas, key_metas_backup,
                   Bucket_num * cur_associativity * sizeof(slot));
            free(key_metas_backup);
            uint64_t end = get_time();
            rehash_cost_time += end - sta;
            return -1;
        }

        /*------------------ 3. bursting function------------------*/

        inline const char* get_first_key_pointer(htrie_map* hm) {
            return hm->get_tail_pointer(get_page_group_id(&first_slot),
                                        &first_slot);
        }

        int cal_common_prefix_len(const char* s1, int cur_longest_prefix_len,
                                  const char* s2, int new_key_size) {
            if (cur_longest_prefix_len > new_key_size) {
                cur_longest_prefix_len = new_key_size;
            }
            for (int i = 0; i != cur_longest_prefix_len; i++) {
                if (s1[i] != s2[i]) {
                    return i;
                }
            }
            return cur_longest_prefix_len;
        }

        inline size_t round_up_2_next_power_2(size_t x) {
            x--;
            x |= x >> 1;   // handle  2 bit numbers
            x |= x >> 2;   // handle  4 bit numbers
            x |= x >> 4;   // handle  8 bit numbers
            x |= x >> 8;   // handle 16 bit numbers
            x |= x >> 16;  // handle 32 bit numbers
            x++;

            return x;
        }

        inline bool need_burst() const {
            return elem_num >= Max_slot_num * Burst_ratio;
        }

        // To turn this(a hashnode) to n trie_node_childs of trie_node linking
        // their hashnode
        void burst(trie_node* p, htrie_map* hm, std::string prefix) {
            burst_total_counter++;

            trie_node* parent_wait_to_be_clean_prefix = p;

            // calculate the capacity of hashnode we need
            const char* first_key_p = get_first_key_pointer(hm);

            for (int i = 0; i != Bucket_num; i++) {
                for (int j = 0; j != Associativity && common_prefix_len != 0;
                     j++) {
                    slot* s = get_slot(i, j);
                    if (s->isEmpty()) break;

                    char* key = hm->get_tail_pointer(get_page_group_id(s), s);

                    // update the common_prefix_len
                    int cur_com_prefix_len = cal_common_prefix_len(
                        first_key_p, common_prefix_len, key, s->get_length());
                    if (common_prefix_len > cur_com_prefix_len) {
                        common_prefix_len = cur_com_prefix_len;
                    }
                }
            }

            map<CharT, uint16_t> element_num_of_1st_char;
            for (int i = 0; i != Bucket_num; i++) {
                for (int j = 0; j != Associativity; j++) {
                    slot* s = get_slot(i, j);
                    if (s->isEmpty()) break;

                    char* key = hm->get_tail_pointer(get_page_group_id(s), s);
                    element_num_of_1st_char[key[common_prefix_len]]++;
                }
            }

            // create the chain with several single trie_node
            // the number of node is common_prefix_len
            for (int i = 0; i != common_prefix_len; i++) {
                if (p == nullptr) {
                    // bursting in a root hashnode
                    // the t_root is update to a empty trie_node
                    p = new trie_node(nullptr);
                    hm->setRoot(p);
                }

                trie_node* cur_trie_node = new trie_node(p);
                p->add_trie_node_child(cur_trie_node, first_key_p[i]);
                p = cur_trie_node;
            }

            // update prefix to (prior prefix + common chain prefix)
            prefix = prefix + string(first_key_p, common_prefix_len);

            map<char, hash_node*> hnode_set;
            // create several hashnode based on the number of the elements that
            // start with the same char
            for (auto it = element_num_of_1st_char.begin();
                 it != element_num_of_1st_char.end(); it++) {
                if (p == nullptr) {
                    // bursting in a root hashnode
                    // the t_root is update to a empty trie_node
                    p = new trie_node(nullptr);
                    hm->setRoot(p);
                }
                trie_node* cur_trie_node = new trie_node(p);
                p->add_trie_node_child(cur_trie_node, it->first);

                size_t expected_associativity =
                    (double)it->second / (double)Bucket_num / Burst_ratio + 1;

                // ceil to the 2
                expected_associativity =
                    round_up_2_next_power_2(expected_associativity);

                hash_node* hnode = new hash_node(
                    cur_trie_node, prefix + it->first, hm, expected_associativity);
                cur_trie_node->set_hash_node_child(hnode);

                hnode_set[it->first] = hnode;
            }

            // a map for the hashnode that insert fail
            map<char, pair<hash_node*, map<string, T>>> burst_again_list;

            // insert the elements with same first char after common_prefix_len
            for (int i = 0; i != Bucket_num; i++) {
                for (int j = 0; j != Associativity; j++) {
                    slot* s = get_slot(i, j);
                    if (s->isEmpty()) break;

                    char* key = hm->get_tail_pointer(get_page_group_id(s), s);

                    size_t pos_move = s->get_pos() + 1 + common_prefix_len;
                    size_t length_left =
                        s->get_length() - common_prefix_len - 1;

                    T v = hm->get_tail_v(get_page_group_id(s), s);
                    hash_node* hnode = hnode_set[key[common_prefix_len]];

                    if (hnode == nullptr) {
                        map<string, T>& temp_elements =
                            burst_again_list[key[common_prefix_len]].second;
                        temp_elements[string(key + common_prefix_len + 1,
                                             length_left)] = v;
                        continue;
                    }

                    // rarely, insert in trie_node
                    if (length_left == 0) {
                        string new_prefix = prefix + key[common_prefix_len];

                        hnode->anode::parent->insert_kv_in_trienode(
                            new_prefix.data(), new_prefix.size(), hm, v);
                        continue;
                    }

                    // normally, insert in hash_node
                    iterator target_it = hnode->search_kv_in_hashnode(
                        hm, key + common_prefix_len + 1, length_left);

                    std::pair<bool, T> res = target_it.insert_hashnode(
                        key + common_prefix_len + 1, length_left, hm, v);

                    // if insert failed, it need burst again
                    if (res.first == false) {
                        // get the element already inserted
                        map<string, T> temp_elements;
                        hnode->get_all_elements(hm, temp_elements);

                        // and current key=value
                        temp_elements[string(key + common_prefix_len + 1,
                                             length_left)] = v;

                        burst_again_list[key[common_prefix_len]] =
                            pair<hash_node*, map<string, T>>(hnode,
                                                             temp_elements);

                        hnode_set[key[common_prefix_len]] = nullptr;
                    }
                }
            }

            // check if there is some failure when insert in hash_node
            for (auto it = burst_again_list.begin();
                 it != burst_again_list.end(); it++) {
                // If burst() fail at this prefix char, we use the old way to
                // burst
                string burst_again_prefix = prefix + it->first;
                pair<hash_node*, map<string, T>>& temp_pair = it->second;
                hash_node* burst_hnode = temp_pair.first;
                burst_hnode->burst_by_elements(temp_pair.second,
                                               burst_hnode->anode::parent, hm,
                                               burst_again_prefix);

                delete burst_hnode;
            }
            if (parent_wait_to_be_clean_prefix != nullptr)
                parent_wait_to_be_clean_prefix->clean_prefix();
            return;
        }

        // optional burst function, time-consuming, but work fine
        // To turn this(a hashnode) to n trie_node_childs of trie_node linking
        // their hashnode
        void burst_by_elements(std::map<std::string, T>& elements, trie_node* p,
                               htrie_map* hm, std::string prefix) {
            // calculate the prefix len first
            auto it = elements.begin();
            string first_string = it->first;
            const char* first_key_p = first_string.data();
            int common_prefix_len = first_string.size();

            // those hash_node that without inserting all elements will have a
            // wrong common_prefix_len, so here we recalculate the
            // common_prefix_len
            for (it++; it != elements.end(); it++) {
                const string& cur_string = it->first;
                int cur_common_prefix =
                    cal_common_prefix_len(first_key_p, common_prefix_len,
                                          cur_string.data(), cur_string.size());
                if (common_prefix_len > cur_common_prefix) {
                    common_prefix_len = cur_common_prefix;
                }
            }

            // create the chain with several single trie_node
            for (int i = 0; i != common_prefix_len; i++) {
                if (p == nullptr) {
                    // bursting in a root hashnode
                    // the t_root is update to a empty trie_node
                    p = new trie_node(nullptr);
                    hm->setRoot(p);
                }

                trie_node* cur_trie_node = new trie_node(p);
                p->add_trie_node_child(cur_trie_node, first_key_p[i]);
                p = cur_trie_node;
            }

            // after now, the subsequent key are different at first
            // char
            std::map<CharT, std::map<std::string, T>> preprocElements;
            for (auto it = elements.begin(); it != elements.end(); it++) {
                const string& cur_string = it->first;
                preprocElements[cur_string[common_prefix_len]]
                               [cur_string.substr(common_prefix_len + 1)] =
                                   it->second;
            }

            // update prefix to (prior prefix + common chain prefix)
            prefix = prefix + string(first_key_p, common_prefix_len);

            // deal with the element with several different head char
            for (auto it = preprocElements.begin(); it != preprocElements.end();
                 it++) {
                if (p == nullptr) {
                    // bursting in a root hashnode
                    // the t_root is update to a empty trie_node
                    p = new trie_node(nullptr);
                    hm->setRoot(p);
                }
                trie_node* cur_trie_node = new trie_node(p);
                p->add_trie_node_child(cur_trie_node, it->first);

                std::map<std::string, T>& curKV = it->second;

                size_t expected_associativity =
                    (double)curKV.size() / (double)Bucket_num / Burst_ratio + 1;

                // ceil to the 2
                expected_associativity =
                    round_up_2_next_power_2(expected_associativity);

                hash_node* hnode = new hash_node(
                    cur_trie_node, prefix + it->first, hm, expected_associativity);
                cur_trie_node->set_hash_node_child(hnode);

                bool stop_insert_and_burst = false;
                for (auto itt = curKV.begin(); itt != curKV.end(); itt++) {
                    const string& temp = itt->first;

                    if (temp.size() == 0) {
                        cur_trie_node->insert_kv_in_trienode(
                            (prefix + it->first).data(), prefix.size() + 1, hm,
                            itt->second);
                        continue;
                    }

                    iterator target_it = hnode->search_kv_in_hashnode(
                        hm, temp.data(), temp.size());
                    std::pair<bool, T> res = target_it.insert_hashnode(
                        temp.data(), temp.size(), hm, itt->second);
                    // if insert failed, it need burst
                    if (res.first == false) {
                        stop_insert_and_burst = true;
                        break;
                    }
                }
                if (stop_insert_and_burst) {
                    hnode->burst_by_elements(curKV, cur_trie_node, hm,
                                             prefix + it->first);
                    delete hnode;
                }
            }
            return;
        }

        /*----------------searching in hash_node----------------*/
        void move_suffix_to_new_page(htrie_map<CharT, T>* hm,
                                  vector<page>& new_normal_page,
                                  vector<page>& new_special_page) {
            for (int i = 0; i != Bucket_num; i++) {
                for (int j = 0; j != cur_associativity; j++) {
                    slot* s = get_slot(i, j);

                    if (s->isEmpty()) break;

                    if (s->isSpecial())
                        s->set_slot(hm->write_kv_to_page(
                            hm->get_tail_pointer(s), s->get_length(),
                            hm->get_tail_v(s), new_special_page));
                    else
                        s->set_slot(hm->write_kv_to_page(
                            hm->get_tail_pointer(s), s->get_length(),
                            hm->get_tail_v(s), new_normal_page));

                    // update first slot
                    first_slot.set_slot(s);
                }
            }
        }

        /*----------------searching in hash_node----------------*/

        std::pair<bool, T> find_kv_in_pages(htrie_map<CharT, T>* hm, slot* s,
                                            const CharT* key,
                                            size_t keysize) {
            if (myTrie::hashRelative::keyEqual(
                    hm->get_tail_pointer(get_page_group_id(s), s),
                                               s->get_length(), key, keysize)) {
                return std::pair<bool, T>(
                    true, *((T*)(hm->get_tail_pointer(get_page_group_id(s), s) +
                                 s->get_length())));
            }
            return std::pair<bool, T>(false, T());
        }

        // return <found?, iterator>
        // iterator:    if slotid==-1, bucket is full
        //              if slotid!=-1, slotid is the insert position
        std::pair<bool, iterator> find_in_bucket(htrie_map<CharT, T>* hm,
                                                 size_t bucketid,
                                                 const CharT* key,
                                                 size_t keysize) {
            // print_key_metas(hm);
            // find the hitted slot in hashnode
            for (int i = 0; i != cur_associativity; i++) {
                slot* target_slot = get_slot(bucketid, i);
                if (target_slot->isEmpty()) {
                    return std::pair<bool, iterator>(
                        false, iterator(false, T(), this, bucketid, i));
                }

                std::pair<bool, T> res =
                    find_kv_in_pages(hm, target_slot, key, keysize);
                if (res.first) {
                    return std::pair<bool, iterator>(
                        true, iterator(true, res.second, this, bucketid, i));
                }
            }
            return std::pair<bool, iterator>(
                false, iterator(false, T(), this, bucketid, -1));
        }

        iterator search_kv_in_hashnode(htrie_map<CharT, T>* hm,
                                       const CharT* key, size_t keysize) {
            // if found the existed target in bucket1 or bucket2, just
            // return the iterator for being modified or read
            size_t bucketId1 =
                myTrie::hashRelative::hash(key, keysize, 1) % Bucket_num;
            std::pair<bool, iterator> res1 =
                find_in_bucket(hm, bucketId1, key, keysize);

            if (res1.first) {
                return res1.second;
            }

            size_t bucketId2 =
                myTrie::hashRelative::hash(key, keysize, 2) % Bucket_num;
            std::pair<bool, iterator> res2 =
                find_in_bucket(hm, bucketId2, key, keysize);

            if (res2.first) {
                return res2.second;
            }

            // if the code reach here it means the target doesn't exist
            // we return the iterator with empty slot
            if (res1.second.slotid != -1) {
                return res1.second;
            } else if (res2.second.slotid != -1) {
                return res2.second;
            }
            // if two bucket are both full, we return the res1's iterator
            // with slotid == -1, and let the
            // 1. findMode: found==false
            // 2. !findMode:found==false, slotid==-1, need to kick some slot
            return res1.second;
        }

        inline bool belong_to_special_group(size_t keysize) {
            return !(keysize < MAX_NORMAL_LEN);
        }

        inline size_t get_page_group_id(size_t keysize) {
            return belong_to_special_group(keysize) ? special_pgid
                                                    : normal_pgid;
        }

        inline size_t get_page_group_id(slot* sl) {
            return belong_to_special_group(sl->get_length()) ? special_pgid
                                                             : normal_pgid;
        }

        std::pair<bool, T> insert_kv_in_hashnode(const CharT* key,
                                                 size_t keysize, htrie_map* hm,
                                                 T v, size_t bucketid,
                                                 int slotid,
                                                   std::string prefix) {
            // if slotid==-1, it denotes that the bucket(bucketid) is full ,
            // so we rehash the key_metas
            if (slotid == -1) {
#ifdef REHASH_BEFORE_EXPAND
                if ((slotid = cuckoo_hash(bucketid, hm)) == -1) {
                    bool expand_success =
                        expand_key_metas_space(cur_associativity << 1, hm);
                    if (!expand_success) {
                        return std::pair<bool, T>(false, T());
                    } else {
                        // if expand success, we get new elem a empty slot
                        // in bucketid
                        for (int i = 0; i != cur_associativity; i++) {
                            slot* empty_slot =
                                key_metas + bucketid * cur_associativity + i;
                            if (empty_slot->isEmpty()) {
                                slotid = i;
                                break;
                            }
                        }
                    }
                }
#else
                bool expand_success =
                    expand_key_metas_space(cur_associativity << 1, hm);
                if (!expand_success) {
                    if ((slotid = cuckoo_hash(bucketid, hm)) == -1) {
                        return std::pair<bool, T>(false, T());
                    }
                } else {
                    // if expand success, we get new elem a empty slot in
                    // bucketid
                    for (int i = 0; i != cur_associativity; i++) {
                        slot* empty_slot =
                            key_metas + bucketid * cur_associativity + i;
                        if (empty_slot->isEmpty()) {
                            slotid = i;
                            break;
                        }
                    }
                    }
#endif
            }

            // now the slotid cannot be -1 and slotid is lower than
            // Associativity
            assert(slotid != -1 && slotid >= 0 && slotid < cur_associativity);

            slot* target_slot = get_slot(bucketid, slotid);

            // call htrie-map function: write_kv_to_page ()
            // return a slot with position that element been written
            target_slot->set_slot(hm->write_kv(belong_to_special_group(keysize),
                                               get_page_group_id(keysize), key,
                                               keysize, v));
            // set v2k
            hm->set_v2k(v, this, get_index(target_slot));

            elem_num++;

            if (need_burst()) {
                burst(this->anode::parent, hm, prefix);

                delete this;
            }

            return std::pair<bool, T>(true, v);
        }
    };

    class page_manager {
        class page_group {
            class page {
               public:
                unsigned int cur_pos;
                char* content;

                page(size_t size_per_page) : cur_pos(0) {
                    content = (char*)malloc(size_per_page);
                }

                void append_impl(const CharT* key, size_t keysize, T& value,
                                 unsigned int alignment = 1) {
                    // append the string
                    std::memcpy(content + cur_pos, key,
                                keysize * sizeof(CharT));

                    // append the value
                    std::memcpy(content + cur_pos + keysize * sizeof(CharT),
                                &value, sizeof(T));

                    cur_pos += calc_align(keysize * sizeof(CharT) + sizeof(T),
                                          alignment);
                }

                // ~page() {
                //     free(content);
                // }
            };

            page* pages;
            int cur_page_id;
            bool is_special;

           public:
            page_group() : pages(nullptr), cur_page_id(0), is_special(false) {}

            void init_pg(int page_number, bool spe) {
                is_special = spe;
                cur_page_id = 0;
                pages = (page*)malloc(
                    sizeof(page) * (spe ? MAX_SPECIAL_PAGE : MAX_NORMAL_PAGE));
                pages[0] = page((spe ? DEFAULT_SPECIAL_Max_bytes_per_kv
                                     : DEFAULT_Max_bytes_per_kv));
            }

            // get function
            inline char* get_tail_pointer_in_page(slot* s) {
                return pages[s->get_page_id()].content + s->get_pos();
                            }

            inline T get_tail_v_in_page(slot* s) {
                T v;
                std::memcpy(&v, get_tail_pointer_in_page(s) + s->get_length(),
                            sizeof(T));
                return v;
                        }

            // set function
            slot write_kv_to_page(const CharT* key, size_t keysize, T v) {
                // allocate space
                size_t need_size = keysize * sizeof(CharT) + sizeof(T);

                if (pages[cur_page_id].cur_pos + need_size >
                    (is_special ? DEFAULT_SPECIAL_Max_bytes_per_kv
                                : Max_bytes_per_kv)) {
                    cur_page_id++;
                    pages[cur_page_id] =
                        page((is_special ? DEFAULT_SPECIAL_Max_bytes_per_kv
                                         : Max_bytes_per_kv));
                    }

                // get page being written
                page& target_page = pages[cur_page_id];

                // record position before updating and status modify
                slot ret_slot =
                    slot(is_special, keysize, target_page.cur_pos, cur_page_id);

                // write content
                target_page.append_impl(key, keysize, v,
                                        is_special ? DEFAULT_SPECIAL_ALIGNMENT
                                                   : DEFAULT_NORMAL_ALIGNMENT);

                return ret_slot;
                }

            page_group(const page_group& orig){
                cout << "calling copy func\n";
                pages = orig.pages;
                cur_page_id = orig.cur_page_id;
                is_special = orig.is_special;
                    }

            ~page_group(){
                cout << "page_group deconstructor" << endl;
                int pages_size =
                    is_special ? MAX_SPECIAL_PAGE : MAX_NORMAL_PAGE;
                for(int i=0; i!=pages_size;i++){
                    free(pages[i].content);
                        }
                free(pages);
                    }
        };

        page_group* normal_pg;
        page_group* special_pg;

        int n_size;
        int s_size;

        enum class init_type : unsigned char {
            IN_NORMAL_GROUP,
            IN_SPECIAL_GROUP,
        };

        void init_a_new_page_group(init_type type) {
            if (type == init_type::IN_SPECIAL_GROUP) {
                s_size++;
                special_pg[s_size - 1].init_pg(MAX_SPECIAL_PAGE, true);
                cout << "special page group increase!" << endl;
                return;
            } else if (type == init_type::IN_NORMAL_GROUP) {
                n_size++;
                normal_pg[n_size - 1].init_pg(MAX_NORMAL_PAGE, false);
                cout << "normal page group increase!" << endl;
                return;
            }else{
                cout << "undefined type!" << endl;
                assert(false);
                exit(0);
                return;
                }
            }


       public:
        page_manager()
            : normal_pg(new page_group[16]),
              special_pg(new page_group[16]),
              n_size(0),
              s_size(0) {
            init_a_new_page_group(init_type::IN_NORMAL_GROUP);
            init_a_new_page_group(init_type::IN_SPECIAL_GROUP);
        }

        inline slot write_kv(bool special, size_t page_group_id, const CharT* key,
                      size_t keysize, T v) {
            return special ? special_pg[page_group_id].write_kv_to_page(
                                 key, keysize, v)
                           : normal_pg[page_group_id].write_kv_to_page(
                                 key, keysize, v);
        }

        inline size_t get_normal_group_id() { return n_size - 1; }
        inline size_t get_special_group_id() { return s_size - 1; }

        // inline size_t get_normal_group_id() { return normal_pg.size() - 1; }
        // inline size_t get_special_group_id() { return special_pg.size() - 1; }


        inline char* get_tail_pointer_in_pm(size_t page_group_id,
                                            slot* s) {
            return s->isSpecial()
                       ? special_pg[page_group_id].get_tail_pointer_in_page(s)
                       : normal_pg[page_group_id].get_tail_pointer_in_page(s);
            }

        inline T get_tail_v_in_pm(size_t page_group_id, slot* s) {
            return s->isSpecial()
                       ? special_pg[page_group_id].get_tail_v_in_page(s)
                       : normal_pg[page_group_id].get_tail_v_in_page(s);
        }
    };

    inline size_t get_normal_group_id() { return pm.get_normal_group_id(); }
    inline size_t get_special_group_id() { return pm.get_special_group_id(); }

    inline char* get_tail_pointer(size_t page_group_id, slot* s) {
        return pm.get_tail_pointer_in_pm(page_group_id, s);
    }

    inline T get_tail_v(size_t page_group_id, slot* s) {
        return pm.get_tail_v_in_pm(page_group_id, s);
    }

    inline slot write_kv(bool is_special, size_t page_group_id, const CharT* key,
                         size_t keysize, T v) {
        return pm.write_kv(is_special, page_group_id, key, keysize, v);
    }

    class SearchPoint {
       public:
        anode* node;
        int index;

        SearchPoint() : node(nullptr), index(-1) {}
        SearchPoint(anode* n, int i) : node(n), index(i) {}

        void set_index(int i) { index = i; }

        std::string get_string(htrie_map<CharT, T>* hm) {
            if (node == nullptr) return string();
            // if the node is trie_node, just return the prefix on node
            if (node->is_trie_node()) {
                return ((trie_node*)node)->get_prefix();
            }

            // get the parent char chain
            trie_node* cur_node = ((hash_node*)node)->anode::parent;
            static char* buf = (char*)malloc(longest_string_size);

            size_t len = cur_node == nullptr ? 0 : cur_node->get_prefix(buf);

            // get tail
            if (index != -1) {
                hash_node* hnode = (hash_node*)node;
                slot* sl = hnode->get_slot(index);

                memcpy(buf + len,
                       hm->get_tail_pointer(hnode->get_page_group_id(sl), sl),
                       sl->get_length());
                len += sl->get_length();
            }
            string res = string(buf, len);
            // free(buf);
            return res;
        }
    };

   public:
    anode* t_root;
    std::map<T, SearchPoint> v2k;
    page_manager pm;

    htrie_map(size_t customized_associativity = DEFAULT_Associativity,
              size_t customized_bucket_count = DEFAULT_Bucket_num,
              size_t customized_byte_per_kv = DEFAULT_Max_bytes_per_kv,
              double customized_burst_ratio = DEFAULT_Burst_ratio)
        : t_root(nullptr) {
        std::cout << "SET UP GROWING-CUCKOOHASH-TRIE MAP\n";
        cout << "GROW_ASSOCIATIVITY\n";

#ifdef REHASH_BEFORE_EXPAND
        std::cout << "REHASH_BEFORE_EXPAND\n";

#else
        std::cout << "EXPAND_BEFORE_REHASH\n";

#endif

#ifdef MAP_REP
        cout << "MAP REPRESENTATION\n";
#endif
#ifdef ARRAY_REP
        cout << "ARRAY REPRESENTATION\n";
#endif
#ifdef LIST_REP
        cout << "LIST REPRESENTATION\n";
#endif

        Associativity = customized_associativity;
        Bucket_num = customized_bucket_count;
        Max_bytes_per_kv = customized_byte_per_kv;
        Burst_ratio = customized_burst_ratio;

        Max_slot_num = Associativity * Bucket_num;
        Max_loop = Max_slot_num * 0.5;

        t_root = new hash_node(nullptr, string(), this, Associativity);
    }

    void set_searchPoint_index(T v, int index) { v2k[v].set_index(index); }

    void set_v2k(T v, anode* node, int index) {
        v2k[v] = SearchPoint(node, index);
    }

    void setRoot(anode* node) { t_root = node; }

    class slot {
       public:
        uint32_t encode;

        slot() : encode(0) {}

        // encode slot as | special | length | position | page_id |
        uint64_t encode_slot(bool spe, uint64_t len, uint64_t po, uint64_t pd) {
            encode = pd;
            if (spe) {
                assert(len < 1 << NBITS_LEN_S &&
                       (po / DEFAULT_SPECIAL_ALIGNMENT) < 1 << NBITS_POS_S &&
                       pd < 1 << NBITS_PID_S);
                encode += ((po / DEFAULT_SPECIAL_ALIGNMENT) << NBITS_PID_S);
                encode += len << (NBITS_PID_S + NBITS_POS_S);
                encode += ((uint64_t)1)
                          << (NBITS_PID_S + NBITS_LEN_S + NBITS_POS_S);
            } else {
                assert(len < 1 << NBITS_LEN &&
                       (po / DEFAULT_NORMAL_ALIGNMENT) < 1 << NBITS_POS &&
                       pd < 1 << NBITS_PID);
                // encode into a 64bit data
                encode += (po / DEFAULT_NORMAL_ALIGNMENT) << NBITS_PID;
                encode += len << (NBITS_PID + NBITS_POS);
                encode += ((uint64_t)0)  << (NBITS_PID + NBITS_LEN + NBITS_POS);
            }

            return encode;
        }

        bool isEmpty() { return get_length() == 0; }

        bool isSpecial() { return get_special(); }

        slot(bool is_special, KeySizeT l, size_t p, size_t pi) {
            encode = encode_slot(is_special, l, p, pi);
        }

        void set_slot(bool is_special, KeySizeT l, size_t p, size_t pi) {
            encode = encode_slot(is_special, l, p, pi);
        }

        void set_slot(slot s) { encode = s.encode; }

        void set_slot(slot* s) { encode = s->encode; }

        bool get_special() {
            return (encode >> (NBITS_PID + NBITS_LEN + NBITS_POS)) == 1;
        }

        size_t get_length() {
            return encode >> (NBITS_LEN + NBITS_POS + NBITS_PID)
                       ? ((encode >> (NBITS_PID_S + NBITS_POS_S)) %
                          (1 << NBITS_LEN_S))
                       : ((encode >> (NBITS_PID + NBITS_POS)) %
                          (1 << NBITS_LEN));
        }

        size_t get_pos() {
            return encode >> (NBITS_PID + NBITS_LEN + NBITS_POS)
                       ? ((encode >> NBITS_PID_S) % (1 << NBITS_POS_S) *
                          DEFAULT_SPECIAL_ALIGNMENT)
                       : ((encode >> NBITS_PID) % (1 << NBITS_POS) *
                          DEFAULT_NORMAL_ALIGNMENT);
        }

        size_t get_page_id() {
            return encode >> (NBITS_PID + NBITS_LEN + NBITS_POS)
                       ? encode % (1 << NBITS_PID_S)
                       : encode % (1 << NBITS_PID);
        }
    };

    inline static unsigned int calc_align(unsigned int n, unsigned align) {
        return ((n + align - 1) & (~(align - 1)));
    }

    // inline char* get_tail_pointer(slot* s) const {
    //     if (s->isSpecial()) {
    //         return special_pages[s->get_page_id()].content + s->get_pos();
    //     }
    //     return normal_pages[s->get_page_id()].content + s->get_pos();
    // }

    // inline char* get_tail_pointer(slot* s, vector<page>& specific_page) const {
    //     if (s->isSpecial()) {
    //         return special_pages[s->get_page_id()].content + s->get_pos();
    //     }
    //     return specific_page[s->get_page_id()].content + s->get_pos();
    // }

    void get_tail_str_v(size_t page_group_id,
                        std::map<std::string, T>& elements, slot* s) {
        char* tail_pointer = get_tail_pointer(page_group_id, s);
        std::string res(tail_pointer, s->get_length());
        std::memcpy(&elements[res], tail_pointer + s->get_length(), sizeof(T));
    }

    // T get_tail_v(slot* s) {
    //     T v;
    //     std::memcpy(&v, get_tail_pointer(s) + s->get_length(), sizeof(T));
    //     return v;
    // }

    // T get_tail_v(slot* s, vector<page>& specific_page) {
    //     T v;
    //     std::memcpy(&v, get_tail_pointer(s, specific_page) + s->get_length(),
    //                 sizeof(T));
    //     return v;
    // }

    slot make_slot(bool is_special, size_t keysize, size_t pos,
                   size_t page_id) {
        return slot(is_special, keysize, pos, page_id);
    }

    // TODO:REMOVE!!!
    // /*------------for clean_prefix()------------------*/
    // inline slot write_kv_to_page(const CharT* key, size_t keysize, T v,
    //                              vector<page>& specific_page) {
    //     return keysize < (1 << NBITS_LEN)
    //                ? write_kv_to_normal_page(key, keysize, v, specific_page)
    //                : write_kv_to_special_page(key, keysize, v, specific_page);
    // }

    // // return: page_id, pos
    // slot write_kv_to_special_page(const CharT* key, size_t keysize, T v,
    //                              vector<page>& specific_page) {
    //     // allocate space
    //     size_t need_size = keysize * sizeof(CharT) + sizeof(T);

    //     size_t cur_page_id = specific_page.size() - 1;

    //     if (specific_page[cur_page_id].cur_pos + need_size > Max_bytes_per_kv) {
    //         page new_page = page(Max_bytes_per_kv * 4);
    //         cur_page_id++;
    //         specific_page.push_back(new_page);
    //     }

    //     // get page being written
    //     page& target_page = specific_page[cur_page_id];

    //     // record position before updating and status modify
    //     slot ret_slot =
    //         make_slot(true, keysize, target_page.cur_pos, cur_page_id);

    //     // write content
    //     target_page.append_impl(key, keysize, v, DEFAULT_SPECIAL_ALIGNMENT);

    //     return ret_slot;
    // }

    // // return: page_id, pos
    // slot write_kv_to_normal_page(const CharT* key, size_t keysize, T v,
    //                              vector<page>& specific_page) {
    //     // allocate space
    //     size_t need_size = keysize * sizeof(CharT) + sizeof(T);

    //     size_t cur_page_id = specific_page.size() - 1;

    //     if (specific_page[cur_page_id].cur_pos + need_size >
    //         Max_bytes_per_kv) {
    //         page new_page = page(Max_bytes_per_kv);
    //         cur_page_id++;
    //         specific_page.push_back(new_page);
    //     }

    //     // get page being written
    //     page& target_page = specific_page[cur_page_id];

    //     // record position before updating and status modify
    //     slot ret_slot =
    //         slot(false, keysize, target_page.cur_pos, specific_page.size() - 1);

    //     // write content
    //     target_page.append_impl(key, keysize, v, DEFAULT_NORMAL_ALIGNMENT);

    //     return ret_slot;
    // }

    class iterator {
       public:
        bool found;
        const T v;
        anode* target_node;
        size_t bucketid;
        int slotid;

        iterator(bool f, T vv, anode* hnode, size_t bid, int sid)
            : found(f), v(vv), target_node(hnode), bucketid(bid), slotid(sid) {}

        std::pair<bool, T> insert_hashnode(const CharT* key, size_t key_size,
                                           htrie_map<CharT, T>* hm, T v) {
            return ((hash_node*)target_node)
                ->insert_kv_in_hashnode(
                    key, key_size, hm, v, bucketid, slotid,
                    ((hash_node*)target_node)->get_prefix());
        }

        std::pair<bool, T> insert_trienode(const CharT* key, size_t key_size,
                                           htrie_map<CharT, T>* hm, T v) {
            return ((trie_node*)target_node)
                ->insert_kv_in_trienode(key, key_size, hm, v);
        }
    };

    std::pair<bool, T> access_kv_in_htrie_map(const CharT* key, size_t key_size,
                                              T v, bool findMode, bool clean_on = true) {
        // if (clean_on && !findMode) clean_prefix();

        // update longest_string_size
        longest_string_size =
            longest_string_size > key_size ? longest_string_size : key_size;

        anode* current_node = t_root;

        for (size_t pos = 0; pos < key_size; pos++) {
            switch (current_node->anode::_node_type) {
                case node_type::MULTI_NODE: {
                    if (!findMode) {
                    } else {
                        size_t jump_pos =
                            ((multi_node*)current_node)->string_keysize_;
                        current_node =
                            ((multi_node*)current_node)
                                ->find_child(key + pos, key_size - pos);
                        if (current_node == nullptr) {
                            return std::pair<bool, T>(false, T());
                        }
                        pos += jump_pos - 1;

                        if (current_node->is_trie_node() &&
                            ((trie_node*)current_node)->get_hash_node_child() !=
                                nullptr &&
                            pos != key_size - 1) {
                            current_node = ((trie_node*)current_node)
                                               ->get_hash_node_child();
                        }
                    }
                } break;
                case node_type::TRIE_NODE: {
                    if (!findMode) {
                        // return the hitted trie_node* or create a new
                        // trie_node with a hash_node son
                        current_node =
                            ((trie_node*)current_node)
                                ->find_trie_node_child(key[pos], key, pos, this);
                    } else {
                        // return the hitted trie_node* or nullptr if not
                        // found
                        current_node = ((trie_node*)current_node)
                                           ->find_trie_node_child(key[pos]);
                        // only in the findMode==true can cause the
                        // current_node to be nullptr
                        if (current_node == nullptr) {
                            return std::pair<bool, T>(false, T());
                        }
                    }

                    if (((trie_node*)current_node)->get_hash_node_child() !=
                            nullptr &&
                        pos != key_size - 1) {
                        current_node =
                            ((trie_node*)current_node)->get_hash_node_child();
                    }
                } break;
                case node_type::HASH_NODE: {
                    iterator it = ((hash_node*)current_node)
                                      ->search_kv_in_hashnode(this, key + pos,
                                                              key_size - pos);
                    if (findMode) {
                        return std::pair<bool, T>(it.found, it.v);
                    } else {
                        pair<bool, T> res = it.insert_hashnode(
                            key + pos, key_size - pos, this, v);
                        if (res.first == false) {
                            // if the insert failed, we burst the
                            // target_hashnode and retry insertion
                            hash_node* hnode_burst_needed =
                                (hash_node*)current_node;

                            hnode_burst_needed->burst(
                                hnode_burst_needed->anode::parent, this,
                                string(key, pos));

                            delete hnode_burst_needed;
                            return access_kv_in_htrie_map(key, key_size, v,
                                                          false, false);
                        }
                        return res;
                    }
                } break;
                default:
                    cout << "wrong type!";
                    exit(0);
            }
        }

        // find a key in trie_node's only value
        iterator it = ((trie_node*)current_node)->search_kv_in_trienode();

        if (findMode) {
            return std::pair<bool, T>(it.found, it.v);
        } else {
            return it.insert_trienode(key, key_size, this, v);
        }
    }

    /*---------------external accessing interface-------------------*/

    // access element
    T searchByKey(std::string key) {
        return access_kv_in_htrie_map(key.data(), key.size(), T(), true).second;
    }

    std::string searchByValue(T v) { return v2k[v].get_string(this); }

    // find operation
    std::pair<bool, T> findByKey(std::string key) {
        return access_kv_in_htrie_map(key.data(), key.size(), T(), true);
    }

    std::pair<bool, std::string> findByValue(T v) {
        if (v2k.find(v) == v2k.end()) {
            return std::pair<bool, T>(false, string());
        } else {
            return std::pair<bool, T>(true, v2k[v].get_string(this));
        }
    }

    std::pair<bool, T> insertKV(std::string key, T v) {
        return access_kv_in_htrie_map(key.data(), key.size(), v, false);
    }

    static inline bool sort_by_hash_val(const pair<string, anode*>& t1,
                                 const pair<string, anode*>& t2) {
        int hash_val1 =
            myTrie::hashRelative::hash(t1.first.data(), t1.first.size());
        int hash_val2 =
            myTrie::hashRelative::hash(t2.first.data(), t2.first.size());

        return hash_val1 < hash_val2;
        // return hash_val1 >= hash_val2;
    }

    /*---------------external cleaning interface-------------------*/
    /*--------------------global shrink node functions--------------------*/
    // TODO: FIX ME! the branch of deciding the type of node is poor
    anode* shrink_node(anode* node) {
        if (node->is_trie_node()) {
            trie_node* cur_node = (trie_node*)node;

            hash_node* hash_node_child = cur_node->get_only_hash_node_child();
            if (hash_node_child != nullptr) {
                return cur_node;
            }

            vector<pair<string, anode*>> traverse_save(
                cur_node->trie_node_childs.size());
            vector<pair<CharT, trie_node*>> next_layer;

            cur_node->trie_node_childs.get_childs_with_char(next_layer);

            bool allow_next_layer = true;
            size_t string_keysize = 0;
            do {
                for (int i = 0; i != next_layer.size(); i++) {
                    CharT c = next_layer[i].first;
                    trie_node* next_layer_trie_node = next_layer[i].second;

                    // current key_string add the next_layer's char
                    string new_key_string = traverse_save[i].first + c;

                    traverse_save[i].first = new_key_string;
                    traverse_save[i].second = next_layer_trie_node;

                    pair<CharT, trie_node*> next_pair;
                    if (next_layer_trie_node->get_only_hash_node_child() ==
                            nullptr &&
                        next_layer_trie_node->get_only_trie_node_child()
                                .second == nullptr) {
                        // have several children
                        allow_next_layer = false;
                    } else {
                        // only have a hash_node child or only have a
                        // trie_node child
                        if (next_layer_trie_node->get_only_hash_node_child() !=
                            nullptr) {
                            next_layer[i] =
                                pair<CharT, trie_node*>(CharT(), nullptr);
                            // stop at this layer
                            allow_next_layer = false;
                            traverse_save[i].second = next_layer_trie_node;
                        } else {
                            next_pair = next_layer_trie_node
                                            ->get_only_trie_node_child();
                            next_layer[i] = next_pair;
                        }
                    }
                }
                if (allow_next_layer) {
                    for (int i = 0; i != next_layer.size(); i++) {
                        delete (next_layer[i].second)->anode::parent;
                    }
                }
                string_keysize++;
            } while (allow_next_layer);

            // construct the target multi_node
            multi_node* target_node = new multi_node(string_keysize, traverse_save.size());

            sort(traverse_save.begin(),traverse_save.end(), sort_by_hash_val);

            for (int i = 0; i != traverse_save.size(); i++) {
                anode* res = shrink_node(traverse_save[i].second);

                // add new child multi_node target_node
                target_node->add_child(traverse_save[i].first, i, res);
            }
            return target_node;
        } else if (node->is_hash_node()) {
            return node;
        } else {
            cout << "program are in a unexpected branch\n";
            assert(false);
            exit(0);
        }
    }

    void shrink() {
        cout << "Shrinking\n";
        uint64_t sta = get_time();
        t_root = shrink_node(t_root);
        uint64_t end = get_time();
        shrink_total_time = end - sta;
    }

    /*--------------------global clean prefix functions--------------------*/
    void traverse_node_for_useless_prefix_cleaning(
        anode* node, vector<page>& new_normal_page,
        vector<page>& new_special_page) {
        if (node->is_trie_node()) {
            vector<anode*> child_nodes;
            ((trie_node*)node)->get_childs_vector(child_nodes);
            for (int i = 0; i != child_nodes.size(); i++) {
                traverse_node_for_useless_prefix_cleaning(
                    child_nodes[i], new_normal_page, new_special_page);
            }
        } else if (node->is_hash_node()) {
            ((hash_node*)(node))
                ->move_suffix_to_new_page(this, new_normal_page,
                                          new_special_page);
        } else {
            cout << "program are in a unexpected branch\n";
            assert(false);
            exit(0);
        }
    }

    // void clean_prefix(bool force_clean = false) {
    //     if (!force_clean)
    //         if ((!(cur_normal_page_id == ((1 << NBITS_PID) - 1) ||
    //                cur_special_page_id == ((1 << NBITS_PID_S) - 1))) ||
    //             force_clean)
    //             return;

    //     // move the old pages to temp vector old_page
    //     vector<page> new_normal_page;
    //     vector<page> new_special_page;

    //     // init the old pages
    //     new_normal_page.push_back(page(Max_bytes_per_kv));
    //     new_special_page.push_back(page(DEFAULT_SPECIAL_Max_bytes_per_kv));

    //     cout << "cleaning\n";
    //     uint64_t sta = get_time();
    //     traverse_node_for_useless_prefix_cleaning(t_root, new_normal_page,
    //                                               new_special_page);
    //     uint64_t end = get_time();
    //     clean_prefix_total_time += end - sta;

    //     cout << "clean res: (" << cur_normal_page_id << " " << cur_special_page_id;
        
    //     //TODO: FIXME: the clean function will increase the memory cost?!?!?!
    //     // Release the char* in pages
    //     for(int i=0;i!=normal_pages.size();i++){
    //         free(normal_pages[i].content);
    //     }

    //     for(int i=0;i!=special_pages.size();i++){
    //         free(special_pages[i].content);
    //     }
        
    //     cur_normal_page_id = new_normal_page.size() - 1;
    //     cur_special_page_id = new_special_page.size() - 1;
    //     normal_pages.swap(new_normal_page);
    //     special_pages.swap(new_special_page);

    //     cout << ") ==>  (" << cur_normal_page_id << " " << cur_special_page_id << ")" << endl;

    //     return;
    // }

    void deleteMyself() {
        map<T, SearchPoint> empty;
        v2k.swap(empty);
        t_root->delete_me();
    }
};  // namespace myTrie

}  // namespace myTrie