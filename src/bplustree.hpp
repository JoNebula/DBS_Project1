#pragma once
#include "record.hpp"
#include <algorithm>
#include <utility>
#include <vector>

// B+-tree of minimum degree d (Cormen-style).
//   max_keys = 2d - 1, min_keys = d - 1.
//   Internal nodes: keys + child pointers only.
//   Leaf nodes: keys + RIDs, linked via `next` for range scans.
//   Search ALWAYS descends to a leaf.
class BPlusTree {
public:
    struct Node {
        bool leaf;
        std::vector<Key>   keys;
        std::vector<RID>   rids;       // valid iff leaf
        std::vector<Node*> children;   // valid iff !leaf, size = keys.size()+1
        Node*              next = nullptr;  // valid iff leaf
        explicit Node(bool is_leaf) : leaf(is_leaf) {}
    };

    explicit BPlusTree(int d) : d_(d) {
        max_keys_ = 2 * d_ - 1;
        min_keys_ = d_ - 1;
        root_ = new Node(true);
    }
    ~BPlusTree() { destroy(root_); }
    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    RID  search(Key k) const;
    void insert(Key k, RID rid);
    bool remove(Key k);

    // Range query: visit (key, rid) pairs in [lo, hi] in ascending order.
    template <class F> void range(Key lo, Key hi, F cb) const;

    std::size_t splits()      const { return splits_; }
    int         height()      const;
    double      utilization() const;
    std::size_t node_count()  const;

private:
    struct InsRes { Node* sib = nullptr; Key sep = 0; };

    int   d_;
    int   max_keys_;
    int   min_keys_;
    Node* root_;
    std::size_t splits_ = 0;

    static void destroy(Node* n);
    InsRes insert_(Node* n, Key k, RID rid);
    bool   remove_(Node* parent, int child_idx, Node* n, Key k);
    void   fix_underflow(Node* parent, int idx);
    Node*  leftmost_leaf(Node* n) const;
    Node*  find_leaf(Key k) const;
    int    descend_index(const Node* n, Key k) const;
    void   walk_stats(const Node* n, std::size_t& nodes, std::size_t& keys) const;
};

inline void BPlusTree::destroy(Node* n) {
    if (!n) return;
    if (!n->leaf) for (auto* c : n->children) destroy(c);
    delete n;
}

inline int BPlusTree::descend_index(const Node* n, Key k) const {
    int i = 0;
    while (i < (int)n->keys.size() && k >= n->keys[i]) ++i;
    return i;
}

inline BPlusTree::Node* BPlusTree::find_leaf(Key k) const {
    Node* n = root_;
    while (!n->leaf) n = n->children[descend_index(n, k)];
    return n;
}

inline RID BPlusTree::search(Key k) const {
    Node* leaf = find_leaf(k);
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), k);
    if (it != leaf->keys.end() && *it == k) {
        return leaf->rids[it - leaf->keys.begin()];
    }
    return -1;
}

template <class F>
inline void BPlusTree::range(Key lo, Key hi, F cb) const {
    Node* leaf = find_leaf(lo);
    while (leaf) {
        for (std::size_t i = 0; i < leaf->keys.size(); ++i) {
            if (leaf->keys[i] < lo) continue;
            if (leaf->keys[i] > hi) return;
            cb(leaf->keys[i], leaf->rids[i]);
        }
        leaf = leaf->next;
    }
}

inline void BPlusTree::insert(Key k, RID rid) {
    InsRes r = insert_(root_, k, rid);
    if (r.sib) {
        Node* nr = new Node(false);
        nr->keys.push_back(r.sep);
        nr->children.push_back(root_);
        nr->children.push_back(r.sib);
        root_ = nr;
    }
}

inline BPlusTree::InsRes BPlusTree::insert_(Node* n, Key k, RID rid) {
    if (n->leaf) {
        auto it = std::lower_bound(n->keys.begin(), n->keys.end(), k);
        int pos = (int)(it - n->keys.begin());
        if (it != n->keys.end() && *it == k) { n->rids[pos] = rid; return {}; }
        n->keys.insert(it, k);
        n->rids.insert(n->rids.begin() + pos, rid);
        if ((int)n->keys.size() <= max_keys_) return {};

        ++splits_;
        int mid = (int)n->keys.size() / 2;
        Node* sib = new Node(true);
        sib->keys.assign(n->keys.begin() + mid, n->keys.end());
        sib->rids.assign(n->rids.begin() + mid, n->rids.end());
        n->keys.resize(mid);
        n->rids.resize(mid);
        sib->next = n->next;
        n->next   = sib;
        return { sib, sib->keys.front() };
    }

    int i = descend_index(n, k);
    InsRes r = insert_(n->children[i], k, rid);
    if (!r.sib) return {};

    n->keys.insert(n->keys.begin() + i, r.sep);
    n->children.insert(n->children.begin() + i + 1, r.sib);
    if ((int)n->keys.size() <= max_keys_) return {};

    ++splits_;
    int mid = (int)n->keys.size() / 2;
    Key up = n->keys[mid];
    Node* sib = new Node(false);
    sib->keys.assign(n->keys.begin() + mid + 1, n->keys.end());
    sib->children.assign(n->children.begin() + mid + 1, n->children.end());
    n->keys.resize(mid);
    n->children.resize(mid + 1);
    return { sib, up };
}

inline bool BPlusTree::remove(Key k) {
    bool ok = remove_(nullptr, -1, root_, k);
    if (!root_->leaf && root_->keys.empty()) {
        Node* old = root_;
        root_ = root_->children[0];
        delete old;
    }
    return ok;
}

inline bool BPlusTree::remove_(Node* parent, int child_idx, Node* n, Key k) {
    bool removed;
    if (n->leaf) {
        auto it = std::lower_bound(n->keys.begin(), n->keys.end(), k);
        if (it == n->keys.end() || *it != k) return false;
        int pos = (int)(it - n->keys.begin());
        n->keys.erase(it);
        n->rids.erase(n->rids.begin() + pos);
        removed = true;
    } else {
        int i = descend_index(n, k);
        removed = remove_(n, i, n->children[i], k);
        if (!removed) return false;
    }
    if (parent && (int)n->keys.size() < min_keys_) fix_underflow(parent, child_idx);
    return removed;
}

inline void BPlusTree::fix_underflow(Node* parent, int idx) {
    Node* c = parent->children[idx];
    Node* L = (idx > 0)                         ? parent->children[idx - 1] : nullptr;
    Node* R = (idx + 1 < (int)parent->children.size()) ? parent->children[idx + 1] : nullptr;

    // Borrow from L
    if (L && (int)L->keys.size() > min_keys_) {
        if (c->leaf) {
            c->keys.insert(c->keys.begin(), L->keys.back());
            c->rids.insert(c->rids.begin(), L->rids.back());
            L->keys.pop_back(); L->rids.pop_back();
            parent->keys[idx - 1] = c->keys.front();
        } else {
            c->keys.insert(c->keys.begin(), parent->keys[idx - 1]);
            c->children.insert(c->children.begin(), L->children.back());
            parent->keys[idx - 1] = L->keys.back();
            L->keys.pop_back();
            L->children.pop_back();
        }
        return;
    }
    // Borrow from R
    if (R && (int)R->keys.size() > min_keys_) {
        if (c->leaf) {
            c->keys.push_back(R->keys.front());
            c->rids.push_back(R->rids.front());
            R->keys.erase(R->keys.begin());
            R->rids.erase(R->rids.begin());
            parent->keys[idx] = R->keys.front();
        } else {
            c->keys.push_back(parent->keys[idx]);
            c->children.push_back(R->children.front());
            parent->keys[idx] = R->keys.front();
            R->keys.erase(R->keys.begin());
            R->children.erase(R->children.begin());
        }
        return;
    }
    // Merge
    if (L) {
        if (c->leaf) {
            L->keys.insert(L->keys.end(), c->keys.begin(), c->keys.end());
            L->rids.insert(L->rids.end(), c->rids.begin(), c->rids.end());
            L->next = c->next;
        } else {
            L->keys.push_back(parent->keys[idx - 1]);
            L->keys.insert(L->keys.end(), c->keys.begin(), c->keys.end());
            L->children.insert(L->children.end(), c->children.begin(), c->children.end());
        }
        parent->keys.erase(parent->keys.begin() + idx - 1);
        parent->children.erase(parent->children.begin() + idx);
        delete c;
    } else {
        if (c->leaf) {
            c->keys.insert(c->keys.end(), R->keys.begin(), R->keys.end());
            c->rids.insert(c->rids.end(), R->rids.begin(), R->rids.end());
            c->next = R->next;
        } else {
            c->keys.push_back(parent->keys[idx]);
            c->keys.insert(c->keys.end(), R->keys.begin(), R->keys.end());
            c->children.insert(c->children.end(), R->children.begin(), R->children.end());
        }
        parent->keys.erase(parent->keys.begin() + idx);
        parent->children.erase(parent->children.begin() + idx + 1);
        delete R;
    }
}

inline int BPlusTree::height() const {
    int h = 1;
    const Node* n = root_;
    while (!n->leaf) { n = n->children[0]; ++h; }
    return h;
}

inline void BPlusTree::walk_stats(const Node* n, std::size_t& nodes, std::size_t& keys) const {
    ++nodes;
    keys += n->keys.size();
    if (!n->leaf) for (const auto* c : n->children) walk_stats(c, nodes, keys);
}

inline std::size_t BPlusTree::node_count() const {
    std::size_t nodes = 0, keys = 0;
    walk_stats(root_, nodes, keys);
    return nodes;
}

inline double BPlusTree::utilization() const {
    std::size_t nodes = 0, keys = 0;
    walk_stats(root_, nodes, keys);
    if (nodes == 0) return 0.0;
    return (double)keys / ((double)nodes * (double)max_keys_);
}
