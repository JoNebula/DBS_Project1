#pragma once
#include "record.hpp"
#include <algorithm>
#include <utility>
#include <vector>

// B*-tree of minimum degree d (Cormen-style; max_keys = 2d - 1).
//   Insert: 2/3 fill — if a child overflows, first try to redistribute with
//           an adjacent sibling; only when both partner nodes are full do we
//           perform the 2-to-3 split (two full nodes + parent separator ->
//           three nodes + two separators).
//   Delete: uses the relaxed B-tree threshold (min_keys = d - 1) so that
//           the merge produces exactly 2d - 1 keys (= max). The 2/3 fill
//           invariant is therefore maintained only on the insertion path.
class BStarTree {
public:
    struct Node {
        bool leaf;
        std::vector<Key>   keys;
        std::vector<RID>   rids;
        std::vector<Node*> children;
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
    };

    explicit BStarTree(int d) : d_(d) {
        max_keys_ = 2 * d_ - 1;
        min_keys_ = d_ - 1;                              // for deletion (B-tree threshold)
        root_ = new Node(true);
    }
    ~BStarTree() { destroy(root_); }
    BStarTree(const BStarTree&) = delete;
    BStarTree& operator=(const BStarTree&) = delete;

    RID  search(Key k) const;
    void insert(Key k, RID rid);
    bool remove(Key k);

    template <class F> void range(Key lo, Key hi, F cb) const { range_(root_, lo, hi, cb); }

    std::size_t splits()              const { return splits_; }
    std::size_t redistributions()     const { return redistributions_; }
    std::size_t two_to_three_splits() const { return ttt_splits_; }
    int         height()              const;
    double      utilization()         const;
    std::size_t node_count()          const;

private:
    int   d_;
    int   max_keys_;
    int   min_keys_;
    Node* root_;
    std::size_t splits_           = 0;
    std::size_t redistributions_  = 0;
    std::size_t ttt_splits_       = 0;

    static void destroy(Node* n);
    RID  search_(const Node* n, Key k) const;

    template <class F> void range_(const Node* n, Key lo, Key hi, F& cb) const {
        int i = 0;
        while (i < (int)n->keys.size() && n->keys[i] < lo) ++i;
        while (i < (int)n->keys.size() && n->keys[i] <= hi) {
            if (!n->leaf) range_(n->children[i], lo, hi, cb);
            cb(n->keys[i], n->rids[i]);
            ++i;
        }
        if (!n->leaf && i < (int)n->children.size())
            range_(n->children[i], lo, hi, cb);
    }


    bool insert_(Node* n, Key k, RID rid);     // returns true if n overflowed
    void fix_overflow(Node* parent, int idx);
    void redistribute(Node* parent, int leftIdx);
    void two_to_three_split(Node* parent, int leftIdx);
    void split_root();

    bool remove_(Node* n, Key k);
    bool remove_internal(Node* n, int idx);
    std::pair<Key, RID> get_pred(Node* n);
    std::pair<Key, RID> get_succ(Node* n);
    void fill_(Node* n, int idx);
    void borrow_from_prev(Node* n, int idx);
    void borrow_from_next(Node* n, int idx);
    void merge_(Node* n, int idx);
    void walk_stats(const Node* n, std::size_t& nodes, std::size_t& keys) const;
};

// ----- search -----
inline void BStarTree::destroy(Node* n) {
    if (!n) return;
    if (!n->leaf) for (auto* c : n->children) destroy(c);
    delete n;
}

inline RID BStarTree::search_(const Node* n, Key k) const {
    int i = 0;
    while (i < (int)n->keys.size() && k > n->keys[i]) ++i;
    if (i < (int)n->keys.size() && n->keys[i] == k) return n->rids[i];
    if (n->leaf) return -1;
    return search_(n->children[i], k);
}
inline RID BStarTree::search(Key k) const { return search_(root_, k); }

// ----- insert -----
inline void BStarTree::insert(Key k, RID rid) {
    if (insert_(root_, k, rid)) split_root();
}

inline bool BStarTree::insert_(Node* n, Key k, RID rid) {
    if (n->leaf) {
        int i = (int)n->keys.size() - 1;
        while (i >= 0 && k < n->keys[i]) --i;
        if (i >= 0 && n->keys[i] == k) { n->rids[i] = rid; return false; }
        n->keys.insert(n->keys.begin() + (i + 1), k);
        n->rids.insert(n->rids.begin() + (i + 1), rid);
        return (int)n->keys.size() > max_keys_;
    }
    int i = (int)n->keys.size() - 1;
    while (i >= 0 && k < n->keys[i]) --i;
    if (i >= 0 && n->keys[i] == k) { n->rids[i] = rid; return false; }
    ++i;
    bool overflow = insert_(n->children[i], k, rid);
    if (overflow) fix_overflow(n, i);
    return (int)n->keys.size() > max_keys_;
}

inline void BStarTree::fix_overflow(Node* parent, int idx) {
    Node* L = (idx > 0) ? parent->children[idx - 1] : nullptr;
    Node* R = (idx + 1 < (int)parent->children.size()) ? parent->children[idx + 1] : nullptr;

    // 1) Redistribute with a non-full sibling
    if (L && (int)L->keys.size() < max_keys_) { redistribute(parent, idx - 1); return; }
    if (R && (int)R->keys.size() < max_keys_) { redistribute(parent, idx);     return; }

    // 2) Both siblings full (or only one exists and is full): 2-to-3 split
    if (R) two_to_three_split(parent, idx);
    else   two_to_three_split(parent, idx - 1);
}

inline void BStarTree::redistribute(Node* parent, int leftIdx) {
    ++redistributions_;
    Node* L = parent->children[leftIdx];
    Node* R = parent->children[leftIdx + 1];

    std::vector<Key> pk(L->keys.begin(), L->keys.end());
    std::vector<RID> pr(L->rids.begin(), L->rids.end());
    pk.push_back(parent->keys[leftIdx]);
    pr.push_back(parent->rids[leftIdx]);
    pk.insert(pk.end(), R->keys.begin(), R->keys.end());
    pr.insert(pr.end(), R->rids.begin(), R->rids.end());

    std::vector<Node*> pc;
    if (!L->leaf) {
        pc.insert(pc.end(), L->children.begin(), L->children.end());
        pc.insert(pc.end(), R->children.begin(), R->children.end());
    }

    int total = (int)pk.size();
    int leftSize = total / 2;            // middle key (idx leftSize) goes up

    L->keys.assign(pk.begin(), pk.begin() + leftSize);
    L->rids.assign(pr.begin(), pr.begin() + leftSize);
    R->keys.assign(pk.begin() + leftSize + 1, pk.end());
    R->rids.assign(pr.begin() + leftSize + 1, pr.end());
    if (!L->leaf) {
        L->children.assign(pc.begin(), pc.begin() + leftSize + 1);
        R->children.assign(pc.begin() + leftSize + 1, pc.end());
    }
    parent->keys[leftIdx] = pk[leftSize];
    parent->rids[leftIdx] = pr[leftSize];
}

inline void BStarTree::two_to_three_split(Node* parent, int leftIdx) {
    ++splits_;
    ++ttt_splits_;
    Node* L = parent->children[leftIdx];
    Node* R = parent->children[leftIdx + 1];

    std::vector<Key> pk(L->keys.begin(), L->keys.end());
    std::vector<RID> pr(L->rids.begin(), L->rids.end());
    pk.push_back(parent->keys[leftIdx]);
    pr.push_back(parent->rids[leftIdx]);
    pk.insert(pk.end(), R->keys.begin(), R->keys.end());
    pr.insert(pr.end(), R->rids.begin(), R->rids.end());

    std::vector<Node*> pc;
    if (!L->leaf) {
        pc.insert(pc.end(), L->children.begin(), L->children.end());
        pc.insert(pc.end(), R->children.begin(), R->children.end());
    }

    int total = (int)pk.size();
    int data  = total - 2;               // 2 separators bubble up
    int aSize = data / 3;
    int bSize = (data + 1) / 3;
    int s1Idx = aSize;
    int bStart = aSize + 1;
    int s2Idx = bStart + bSize;
    int cStart = s2Idx + 1;

    Node* A = L;
    Node* B = new Node(L->leaf);
    Node* C = R;

    A->keys.assign(pk.begin(), pk.begin() + aSize);
    A->rids.assign(pr.begin(), pr.begin() + aSize);
    B->keys.assign(pk.begin() + bStart, pk.begin() + bStart + bSize);
    B->rids.assign(pr.begin() + bStart, pr.begin() + bStart + bSize);
    C->keys.assign(pk.begin() + cStart, pk.end());
    C->rids.assign(pr.begin() + cStart, pr.end());
    if (!A->leaf) {
        A->children.assign(pc.begin(),                       pc.begin() + aSize + 1);
        B->children.assign(pc.begin() + bStart,              pc.begin() + bStart + bSize + 1);
        C->children.assign(pc.begin() + cStart,              pc.end());
    }

    parent->keys[leftIdx] = pk[s1Idx];
    parent->rids[leftIdx] = pr[s1Idx];
    parent->keys.insert(parent->keys.begin() + leftIdx + 1, pk[s2Idx]);
    parent->rids.insert(parent->rids.begin() + leftIdx + 1, pr[s2Idx]);
    parent->children.insert(parent->children.begin() + leftIdx + 1, B);
}

inline void BStarTree::split_root() {
    ++splits_;
    Node* old = root_;
    int mid = (int)old->keys.size() / 2;
    Node* sib = new Node(old->leaf);
    sib->keys.assign(old->keys.begin() + mid + 1, old->keys.end());
    sib->rids.assign(old->rids.begin() + mid + 1, old->rids.end());
    if (!old->leaf) {
        sib->children.assign(old->children.begin() + mid + 1, old->children.end());
        old->children.resize(mid + 1);
    }
    Key pk = old->keys[mid];
    RID pr = old->rids[mid];
    old->keys.resize(mid);
    old->rids.resize(mid);

    Node* nr = new Node(false);
    nr->keys.push_back(pk);
    nr->rids.push_back(pr);
    nr->children.push_back(old);
    nr->children.push_back(sib);
    root_ = nr;
}

// ----- delete (B-tree style with the higher 2/3 threshold) -----
inline bool BStarTree::remove(Key k) {
    bool ok = remove_(root_, k);
    if (!root_->leaf && root_->keys.empty()) {
        Node* old = root_;
        root_ = root_->children[0];
        delete old;
    }
    return ok;
}

inline bool BStarTree::remove_(Node* n, Key k) {
    int i = 0;
    while (i < (int)n->keys.size() && k > n->keys[i]) ++i;

    if (i < (int)n->keys.size() && n->keys[i] == k) {
        if (n->leaf) {
            n->keys.erase(n->keys.begin() + i);
            n->rids.erase(n->rids.begin() + i);
            return true;
        }
        return remove_internal(n, i);
    }
    if (n->leaf) return false;

    bool last = (i == (int)n->keys.size());
    if ((int)n->children[i]->keys.size() <= min_keys_) fill_(n, i);
    if (last && i > (int)n->keys.size()) return remove_(n->children[i - 1], k);
    return remove_(n->children[i], k);
}

inline bool BStarTree::remove_internal(Node* n, int idx) {
    Key k = n->keys[idx];
    if ((int)n->children[idx]->keys.size() > min_keys_) {
        auto [pk, pr] = get_pred(n->children[idx]);
        n->keys[idx] = pk; n->rids[idx] = pr;
        return remove_(n->children[idx], pk);
    }
    if ((int)n->children[idx + 1]->keys.size() > min_keys_) {
        auto [sk, sr] = get_succ(n->children[idx + 1]);
        n->keys[idx] = sk; n->rids[idx] = sr;
        return remove_(n->children[idx + 1], sk);
    }
    merge_(n, idx);
    return remove_(n->children[idx], k);
}

inline std::pair<Key, RID> BStarTree::get_pred(Node* n) {
    while (!n->leaf) n = n->children.back();
    return { n->keys.back(), n->rids.back() };
}
inline std::pair<Key, RID> BStarTree::get_succ(Node* n) {
    while (!n->leaf) n = n->children.front();
    return { n->keys.front(), n->rids.front() };
}

inline void BStarTree::fill_(Node* n, int idx) {
    if (idx > 0 && (int)n->children[idx - 1]->keys.size() > min_keys_) {
        borrow_from_prev(n, idx);
    } else if (idx < (int)n->keys.size() && (int)n->children[idx + 1]->keys.size() > min_keys_) {
        borrow_from_next(n, idx);
    } else {
        if (idx < (int)n->keys.size()) merge_(n, idx);
        else                            merge_(n, idx - 1);
    }
}

inline void BStarTree::borrow_from_prev(Node* n, int idx) {
    Node* c = n->children[idx];
    Node* s = n->children[idx - 1];
    c->keys.insert(c->keys.begin(), n->keys[idx - 1]);
    c->rids.insert(c->rids.begin(), n->rids[idx - 1]);
    if (!c->leaf) {
        c->children.insert(c->children.begin(), s->children.back());
        s->children.pop_back();
    }
    n->keys[idx - 1] = s->keys.back();
    n->rids[idx - 1] = s->rids.back();
    s->keys.pop_back(); s->rids.pop_back();
}

inline void BStarTree::borrow_from_next(Node* n, int idx) {
    Node* c = n->children[idx];
    Node* s = n->children[idx + 1];
    c->keys.push_back(n->keys[idx]);
    c->rids.push_back(n->rids[idx]);
    if (!c->leaf) {
        c->children.push_back(s->children.front());
        s->children.erase(s->children.begin());
    }
    n->keys[idx] = s->keys.front();
    n->rids[idx] = s->rids.front();
    s->keys.erase(s->keys.begin());
    s->rids.erase(s->rids.begin());
}

inline void BStarTree::merge_(Node* n, int idx) {
    Node* c = n->children[idx];
    Node* s = n->children[idx + 1];
    c->keys.push_back(n->keys[idx]);
    c->rids.push_back(n->rids[idx]);
    c->keys.insert(c->keys.end(), s->keys.begin(), s->keys.end());
    c->rids.insert(c->rids.end(), s->rids.begin(), s->rids.end());
    if (!c->leaf)
        c->children.insert(c->children.end(), s->children.begin(), s->children.end());
    n->keys.erase(n->keys.begin() + idx);
    n->rids.erase(n->rids.begin() + idx);
    n->children.erase(n->children.begin() + idx + 1);
    delete s;
}

inline int BStarTree::height() const {
    int h = 1;
    const Node* n = root_;
    while (!n->leaf) { n = n->children[0]; ++h; }
    return h;
}

inline void BStarTree::walk_stats(const Node* n, std::size_t& nodes, std::size_t& keys) const {
    ++nodes;
    keys += n->keys.size();
    if (!n->leaf) for (const auto* c : n->children) walk_stats(c, nodes, keys);
}

inline std::size_t BStarTree::node_count() const {
    std::size_t nodes = 0, keys = 0;
    walk_stats(root_, nodes, keys);
    return nodes;
}

inline double BStarTree::utilization() const {
    std::size_t nodes = 0, keys = 0;
    walk_stats(root_, nodes, keys);
    if (nodes == 0) return 0.0;
    return (double)keys / ((double)nodes * (double)max_keys_);
}
