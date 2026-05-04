#pragma once
#include "record.hpp"
#include <algorithm>
#include <utility>
#include <vector>

// B-tree of minimum degree d (Cormen-style).
//   max_keys = 2d - 1
//   min_keys = d - 1   (for non-root nodes)
// Internal nodes store (key, RID) entries; search may terminate at internal nodes.
class BTree {
public:
    struct Node {
        bool leaf;
        std::vector<Key>   keys;
        std::vector<RID>   rids;        // parallel to keys
        std::vector<Node*> children;    // size = keys.size() + 1 if !leaf
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
    };

    explicit BTree(int d) : d_(d) {
        max_keys_ = 2 * d_ - 1;
        min_keys_ = d_ - 1;
        root_ = new Node(true);
    }
    ~BTree() { destroy(root_); }
    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;

    RID  search(Key k) const { return search_(root_, k); }
    void insert(Key k, RID rid);
    bool remove(Key k);

    template <class F> void range(Key lo, Key hi, F cb) const { range_(root_, lo, hi, cb); }

    // statistics
    std::size_t splits()      const { return splits_; }
    int         height()      const;
    double      utilization() const;     // average filled-slots / max_keys across all nodes
    std::size_t node_count()  const;

private:
    int   d_;
    int   max_keys_;
    int   min_keys_;
    Node* root_;
    std::size_t splits_ = 0;

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

    void split_child(Node* parent, int idx);
    void insert_nonfull(Node* n, Key k, RID rid);

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

inline void BTree::destroy(Node* n) {
    if (!n) return;
    if (!n->leaf) for (auto* c : n->children) destroy(c);
    delete n;
}

inline RID BTree::search_(const Node* n, Key k) const {
    int i = 0;
    while (i < (int)n->keys.size() && k > n->keys[i]) ++i;
    if (i < (int)n->keys.size() && n->keys[i] == k) return n->rids[i];
    if (n->leaf) return -1;
    return search_(n->children[i], k);
}

inline void BTree::split_child(Node* parent, int idx) {
    ++splits_;
    Node* child = parent->children[idx];
    int mid = (int)child->keys.size() / 2;
    Node* sib = new Node(child->leaf);

    sib->keys.assign(child->keys.begin() + mid + 1, child->keys.end());
    sib->rids.assign(child->rids.begin() + mid + 1, child->rids.end());
    if (!child->leaf) {
        sib->children.assign(child->children.begin() + mid + 1, child->children.end());
        child->children.resize(mid + 1);
    }

    Key pkey = child->keys[mid];
    RID prid = child->rids[mid];
    child->keys.resize(mid);
    child->rids.resize(mid);

    parent->keys.insert(parent->keys.begin() + idx, pkey);
    parent->rids.insert(parent->rids.begin() + idx, prid);
    parent->children.insert(parent->children.begin() + idx + 1, sib);
}

inline void BTree::insert(Key k, RID rid) {
    if ((int)root_->keys.size() == max_keys_) {
        Node* nr = new Node(false);
        nr->children.push_back(root_);
        root_ = nr;
        split_child(nr, 0);
    }
    insert_nonfull(root_, k, rid);
}

inline void BTree::insert_nonfull(Node* n, Key k, RID rid) {
    int i = (int)n->keys.size() - 1;
    if (n->leaf) {
        while (i >= 0 && k < n->keys[i]) --i;
        if (i >= 0 && n->keys[i] == k) { n->rids[i] = rid; return; }
        n->keys.insert(n->keys.begin() + (i + 1), k);
        n->rids.insert(n->rids.begin() + (i + 1), rid);
        return;
    }
    while (i >= 0 && k < n->keys[i]) --i;
    if (i >= 0 && n->keys[i] == k) { n->rids[i] = rid; return; }
    ++i;
    if ((int)n->children[i]->keys.size() == max_keys_) {
        split_child(n, i);
        if (k > n->keys[i])      ++i;
        else if (k == n->keys[i]) { n->rids[i] = rid; return; }
    }
    insert_nonfull(n->children[i], k, rid);
}

inline bool BTree::remove(Key k) {
    bool ok = remove_(root_, k);
    if (!root_->leaf && root_->keys.empty()) {
        Node* old = root_;
        root_ = root_->children[0];
        delete old;
    }
    return ok;
}

inline bool BTree::remove_(Node* n, Key k) {
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
    if ((int)n->children[i]->keys.size() == min_keys_) fill_(n, i);
    if (last && i > (int)n->keys.size()) return remove_(n->children[i - 1], k);
    return remove_(n->children[i], k);
}

inline bool BTree::remove_internal(Node* n, int idx) {
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

inline std::pair<Key, RID> BTree::get_pred(Node* n) {
    while (!n->leaf) n = n->children.back();
    return { n->keys.back(), n->rids.back() };
}
inline std::pair<Key, RID> BTree::get_succ(Node* n) {
    while (!n->leaf) n = n->children.front();
    return { n->keys.front(), n->rids.front() };
}

inline void BTree::fill_(Node* n, int idx) {
    if (idx > 0 && (int)n->children[idx - 1]->keys.size() > min_keys_) {
        borrow_from_prev(n, idx);
    } else if (idx < (int)n->keys.size() && (int)n->children[idx + 1]->keys.size() > min_keys_) {
        borrow_from_next(n, idx);
    } else {
        if (idx < (int)n->keys.size()) merge_(n, idx);
        else                            merge_(n, idx - 1);
    }
}

inline void BTree::borrow_from_prev(Node* n, int idx) {
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

inline void BTree::borrow_from_next(Node* n, int idx) {
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

inline void BTree::merge_(Node* n, int idx) {
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

inline int BTree::height() const {
    int h = 1;
    const Node* n = root_;
    while (!n->leaf) { n = n->children[0]; ++h; }
    return h;
}

inline void BTree::walk_stats(const Node* n, std::size_t& nodes, std::size_t& keys) const {
    ++nodes;
    keys += n->keys.size();
    if (!n->leaf) for (const auto* c : n->children) walk_stats(c, nodes, keys);
}

inline std::size_t BTree::node_count() const {
    std::size_t nodes = 0, keys = 0;
    walk_stats(root_, nodes, keys);
    return nodes;
}

inline double BTree::utilization() const {
    std::size_t nodes = 0, keys = 0;
    walk_stats(root_, nodes, keys);
    if (nodes == 0) return 0.0;
    return (double)keys / ((double)nodes * (double)max_keys_);
}
