#include "benchmark.hpp"
#include "bplustree.hpp"
#include "bstartree.hpp"
#include "btree.hpp"
#include "csv_loader.hpp"
#include "record.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

// ----- experiment configuration -----
constexpr int  kOrders[]      = {3, 4, 5, 7, 10, 15, 20, 30, 50};
constexpr int  kTrials        = 3;
constexpr int  kSearchN       = 10000;
constexpr int  kFixedOrder    = 10;          // for ordering / width / trajectory experiments
constexpr int  kDeletePct1    = 10;
constexpr int  kDeletePct2    = 20;
constexpr int  kTrajStep      = 10000;       // splits-trajectory checkpoint interval
constexpr Key  kRangeLo       = 202000000;
constexpr Key  kRangeHi       = 202100000;
constexpr unsigned kBaseSeed  = 42;

// ----- row types -----
struct InsertionRow { std::string tree; int order, trial; double time_ms;
                      std::size_t splits; double utilization; int height; std::size_t nodes; };
struct SearchRow    { std::string tree; int order, trial, n; double total_us, mean_us; int hits; };
struct RangeRow     { std::string tree; int order, trial; double time_ms; int hits;
                      double avg_gpa, avg_height; };
struct DeleteRow    { std::string tree; int order, trial; std::string batch; int n;
                      double time_ms; int height_after; double util_after; };
struct OrderingRow  { std::string tree; std::string ordering; int trial; double time_ms;
                      std::size_t splits; double utilization; int height; };
struct WidthRow     { std::string tree; long long width; int trial; double time_ms; int hits; };
struct TrajRow      { std::string tree; std::size_t n_inserted, splits, nodes; int height;
                      double utilization; };

// ============================================================
// Basic experiments — sweep across (tree, order, trial)
// ============================================================
template <class Tree>
void basic_for_tree(const std::string& tree_name,
                    const std::vector<StudentRecord>& records,
                    int order, int trial,
                    std::vector<InsertionRow>& ins,
                    std::vector<SearchRow>&    srch,
                    std::vector<RangeRow>&     rng,
                    std::vector<DeleteRow>&    del)
{
    Tree tree(order);

    // Each trial inserts in a shuffled order so the structure differs.
    std::vector<std::size_t> idx(records.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng_ord(kBaseSeed + (unsigned)trial * 1009u);
    std::shuffle(idx.begin(), idx.end(), rng_ord);

    Timer t; t.start();
    for (std::size_t i : idx) tree.insert(records[i].student_id, (RID)i);
    double ins_ms = t.stop_ms();
    ins.push_back({ tree_name, order, trial, ins_ms, tree.splits(),
                    tree.utilization(), tree.height(), tree.node_count() });

    // ---- Point search (same query set across trees so results are comparable) ----
    std::mt19937 rng_pt(kBaseSeed + (unsigned)trial);
    std::uniform_int_distribution<std::size_t> pick(0, records.size() - 1);
    std::vector<Key> queries; queries.reserve(kSearchN);
    for (int i = 0; i < kSearchN; ++i) queries.push_back(records[pick(rng_pt)].student_id);

    int hits = 0;
    t.start();
    for (Key q : queries) if (tree.search(q) >= 0) ++hits;
    double s_us = t.stop_us();
    srch.push_back({ tree_name, order, trial, kSearchN, s_us, s_us / kSearchN, hits });

    // ---- Range query ----
    double sum_gpa = 0.0, sum_h = 0.0; int cnt_male = 0;
    auto cb = [&](Key, RID rid) {
        const auto& r = records[rid];
        if (r.gender == "Male") { sum_gpa += r.gpa; sum_h += r.height; ++cnt_male; }
    };
    t.start();
    tree.range(kRangeLo, kRangeHi, cb);
    double rng_ms = t.stop_ms();
    rng.push_back({ tree_name, order, trial, rng_ms, cnt_male,
                    cnt_male ? sum_gpa / cnt_male : 0.0,
                    cnt_male ? sum_h   / cnt_male : 0.0 });

    // ---- Deletion ----
    std::vector<Key> ids; ids.reserve(records.size());
    for (auto& r : records) ids.push_back(r.student_id);
    std::mt19937 rng_del(kBaseSeed + (unsigned)trial * 7919u);
    std::shuffle(ids.begin(), ids.end(), rng_del);

    int n1 = (int)records.size() * kDeletePct1 / 100;
    int n2 = (int)records.size() * kDeletePct2 / 100 - n1;

    t.start();
    int ok1 = 0;
    for (int i = 0; i < n1; ++i) if (tree.remove(ids[i])) ++ok1;
    double d1_ms = t.stop_ms();
    del.push_back({ tree_name, order, trial, "10pct", ok1, d1_ms,
                    tree.height(), tree.utilization() });

    t.start();
    int ok2 = 0;
    for (int i = n1; i < n1 + n2; ++i) if (tree.remove(ids[i])) ++ok2;
    double d2_ms = t.stop_ms();
    del.push_back({ tree_name, order, trial, "20pct_total", ok1 + ok2, d2_ms,
                    tree.height(), tree.utilization() });
}

void run_basic_experiments(const std::vector<StudentRecord>& records,
                           std::vector<InsertionRow>& ins,
                           std::vector<SearchRow>&    srch,
                           std::vector<RangeRow>&     rng,
                           std::vector<DeleteRow>&    del)
{
    for (int trial = 1; trial <= kTrials; ++trial) {
        for (int order : kOrders) {
            std::cerr << "  basic [trial=" << trial << ", d=" << order << "] ... " << std::flush;
            basic_for_tree<BTree>     ("Btree",     records, order, trial, ins, srch, rng, del);
            basic_for_tree<BStarTree> ("Bstartree", records, order, trial, ins, srch, rng, del);
            basic_for_tree<BPlusTree> ("Bplustree", records, order, trial, ins, srch, rng, del);
            std::cerr << "done\n";
        }
    }
}

// ============================================================
// Insert ordering: how does input order affect tree structure?
// ============================================================
template <class Tree>
void ordering_for_tree(const std::string& tree_name,
                       const std::vector<StudentRecord>& records,
                       const std::string& ordering,
                       const std::vector<std::size_t>& order_idx,
                       int trial,
                       std::vector<OrderingRow>& out)
{
    Tree tree(kFixedOrder);
    Timer t; t.start();
    for (std::size_t i : order_idx) tree.insert(records[i].student_id, (RID)i);
    double ms = t.stop_ms();
    out.push_back({ tree_name, ordering, trial, ms, tree.splits(),
                    tree.utilization(), tree.height() });
}

void run_insert_ordering(const std::vector<StudentRecord>& records,
                         std::vector<OrderingRow>& out)
{
    const std::size_t N = records.size();
    std::vector<std::size_t> id_idx(N);
    std::iota(id_idx.begin(), id_idx.end(), 0);

    // ascending by Student ID
    std::vector<std::size_t> sorted_idx = id_idx;
    std::sort(sorted_idx.begin(), sorted_idx.end(),
              [&](std::size_t a, std::size_t b) {
                  return records[a].student_id < records[b].student_id;
              });
    std::vector<std::size_t> reverse_idx(sorted_idx.rbegin(), sorted_idx.rend());

    auto run_one = [&](const std::string& name,
                       const std::vector<std::size_t>& idx, int trial) {
        std::cerr << "  ordering [" << name << ", trial=" << trial << "] ..." << std::endl;
        ordering_for_tree<BTree>     ("Btree",     records, name, idx, trial, out);
        ordering_for_tree<BStarTree> ("Bstartree", records, name, idx, trial, out);
        ordering_for_tree<BPlusTree> ("Bplustree", records, name, idx, trial, out);
    };

    // sorted/reverse are deterministic — single trial each.
    run_one("sorted",  sorted_idx,  1);
    run_one("reverse", reverse_idx, 1);

    // csv (original CSV order) and shuffled — multiple trials for shuffled.
    run_one("csv", id_idx, 1);
    for (int trial = 1; trial <= kTrials; ++trial) {
        std::vector<std::size_t> sh = id_idx;
        std::mt19937 g(kBaseSeed + (unsigned)trial * 1009u);
        std::shuffle(sh.begin(), sh.end(), g);
        run_one("shuffle", sh, trial);
    }
}

// ============================================================
// Range width sweep
// ============================================================
template <class Tree>
void width_for_tree(const std::string& tree_name,
                    Tree& tree,
                    long long width,
                    int trial,
                    Key span_lo, Key span_hi,
                    std::mt19937& rng,
                    std::vector<WidthRow>& out)
{
    // pick a random start such that [start, start+width] lies inside [span_lo, span_hi]
    long long max_start = (span_hi - span_lo) - width;
    if (max_start < 0) max_start = 0;
    std::uniform_int_distribution<long long> pick(0, max_start);
    Key lo = span_lo + pick(rng);
    Key hi = lo + width;

    int hits = 0;
    auto cb = [&](Key, RID) { ++hits; };
    Timer t; t.start();
    tree.range(lo, hi, cb);
    double ms = t.stop_ms();
    out.push_back({ tree_name, width, trial, ms, hits });
}

void run_range_width(const std::vector<StudentRecord>& records,
                     std::vector<WidthRow>& out)
{
    const std::vector<long long> widths = { 100LL, 1000LL, 10000LL, 100000LL,
                                            1000000LL, 10000000LL };

    Key span_lo = std::numeric_limits<Key>::max();
    Key span_hi = std::numeric_limits<Key>::min();
    for (auto& r : records) {
        if (r.student_id < span_lo) span_lo = r.student_id;
        if (r.student_id > span_hi) span_hi = r.student_id;
    }

    BTree     bt(kFixedOrder);
    BStarTree bs(kFixedOrder);
    BPlusTree bp(kFixedOrder);
    for (std::size_t i = 0; i < records.size(); ++i) {
        bt.insert(records[i].student_id, (RID)i);
        bs.insert(records[i].student_id, (RID)i);
        bp.insert(records[i].student_id, (RID)i);
    }

    for (long long w : widths) {
        std::cerr << "  range_width [w=" << w << "] ..." << std::endl;
        for (int trial = 1; trial <= kTrials; ++trial) {
            std::mt19937 g(kBaseSeed + (unsigned)trial * 3001u);
            std::mt19937 g2 = g, g3 = g;          // identical starts -> same query each tree
            width_for_tree("Btree",     bt, w, trial, span_lo, span_hi, g,  out);
            width_for_tree("Bstartree", bs, w, trial, span_lo, span_hi, g2, out);
            width_for_tree("Bplustree", bp, w, trial, span_lo, span_hi, g3, out);
        }
    }
}

// ============================================================
// Splits trajectory: cumulative splits + height growth as we insert
// ============================================================
template <class Tree>
void traj_for_tree(const std::string& tree_name,
                   const std::vector<StudentRecord>& records,
                   const std::vector<std::size_t>& order_idx,
                   std::vector<TrajRow>& out)
{
    Tree tree(kFixedOrder);
    for (std::size_t i = 0; i < order_idx.size(); ++i) {
        tree.insert(records[order_idx[i]].student_id, (RID)order_idx[i]);
        std::size_t n = i + 1;
        if (n % kTrajStep == 0 || n == order_idx.size()) {
            out.push_back({ tree_name, n, tree.splits(), tree.node_count(),
                            tree.height(), tree.utilization() });
        }
    }
}

void run_splits_trajectory(const std::vector<StudentRecord>& records,
                           std::vector<TrajRow>& out)
{
    std::cerr << "  splits_trajectory (shuffled, d=" << kFixedOrder << ") ..." << std::endl;
    std::vector<std::size_t> idx(records.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 g(kBaseSeed);
    std::shuffle(idx.begin(), idx.end(), g);

    traj_for_tree<BTree>     ("Btree",     records, idx, out);
    traj_for_tree<BStarTree> ("Bstartree", records, idx, out);
    traj_for_tree<BPlusTree> ("Bplustree", records, idx, out);
}

// ============================================================
// CSV writers
// ============================================================
void write_insertion(const std::string& path, const std::vector<InsertionRow>& v) {
    std::ofstream f(path);
    f << "tree,order,trial,time_ms,splits,utilization,height,node_count\n";
    for (auto& r : v)
        f << r.tree << ',' << r.order << ',' << r.trial << ',' << r.time_ms << ','
          << r.splits << ',' << r.utilization << ',' << r.height << ',' << r.nodes << '\n';
}
void write_search(const std::string& path, const std::vector<SearchRow>& v) {
    std::ofstream f(path);
    f << "tree,order,trial,n_queries,total_us,mean_us,hits\n";
    for (auto& r : v)
        f << r.tree << ',' << r.order << ',' << r.trial << ',' << r.n << ','
          << r.total_us << ',' << r.mean_us << ',' << r.hits << '\n';
}
void write_range(const std::string& path, const std::vector<RangeRow>& v) {
    std::ofstream f(path);
    f << "tree,order,trial,time_ms,male_hits,avg_gpa,avg_height\n";
    for (auto& r : v)
        f << r.tree << ',' << r.order << ',' << r.trial << ',' << r.time_ms << ','
          << r.hits << ',' << r.avg_gpa << ',' << r.avg_height << '\n';
}
void write_delete(const std::string& path, const std::vector<DeleteRow>& v) {
    std::ofstream f(path);
    f << "tree,order,trial,batch,n_deleted,time_ms,height_after,util_after\n";
    for (auto& r : v)
        f << r.tree << ',' << r.order << ',' << r.trial << ',' << r.batch << ','
          << r.n << ',' << r.time_ms << ',' << r.height_after << ',' << r.util_after << '\n';
}
void write_ordering(const std::string& path, const std::vector<OrderingRow>& v) {
    std::ofstream f(path);
    f << "tree,ordering,trial,time_ms,splits,utilization,height\n";
    for (auto& r : v)
        f << r.tree << ',' << r.ordering << ',' << r.trial << ',' << r.time_ms << ','
          << r.splits << ',' << r.utilization << ',' << r.height << '\n';
}
void write_width(const std::string& path, const std::vector<WidthRow>& v) {
    std::ofstream f(path);
    f << "tree,width,trial,time_ms,hits\n";
    for (auto& r : v)
        f << r.tree << ',' << r.width << ',' << r.trial << ',' << r.time_ms << ','
          << r.hits << '\n';
}
void write_traj(const std::string& path, const std::vector<TrajRow>& v) {
    std::ofstream f(path);
    f << "tree,n_inserted,splits,nodes,height,utilization\n";
    for (auto& r : v)
        f << r.tree << ',' << r.n_inserted << ',' << r.splits << ',' << r.nodes << ','
          << r.height << ',' << r.utilization << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    std::string csv_path    = (argc > 1) ? argv[1] : "data/student.csv";
    std::string results_dir = (argc > 2) ? argv[2] : "results";

    std::cerr << "Loading " << csv_path << " ..." << std::endl;
    Timer load; load.start();
    auto records = load_csv(csv_path);
    std::cerr << "Loaded " << records.size() << " records in "
              << load.stop_ms() << " ms\n" << std::endl;

    std::vector<InsertionRow> ins;
    std::vector<SearchRow>    srch;
    std::vector<RangeRow>     rng;
    std::vector<DeleteRow>    del;
    std::vector<OrderingRow>  ord;
    std::vector<WidthRow>     wid;
    std::vector<TrajRow>      tr;

    std::cerr << "[1/4] basic experiments (order sweep × trials)" << std::endl;
    run_basic_experiments(records, ins, srch, rng, del);

    std::cerr << "\n[2/4] insert-ordering experiment" << std::endl;
    run_insert_ordering(records, ord);

    std::cerr << "\n[3/4] range-width sweep" << std::endl;
    run_range_width(records, wid);

    std::cerr << "\n[4/4] splits trajectory" << std::endl;
    run_splits_trajectory(records, tr);

    write_insertion(results_dir + "/insertion.csv",       ins);
    write_search   (results_dir + "/search.csv",          srch);
    write_range    (results_dir + "/range.csv",           rng);
    write_delete   (results_dir + "/deletion.csv",        del);
    write_ordering (results_dir + "/insert_ordering.csv", ord);
    write_width    (results_dir + "/range_width.csv",     wid);
    write_traj     (results_dir + "/splits_traj.csv",     tr);

    std::cerr << "\nWrote results to " << results_dir << "/" << std::endl;
    return 0;
}
