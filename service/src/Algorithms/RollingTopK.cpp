#include "Algorithms/RollingTopK.h"

namespace Alg {

namespace {

// 小顶堆的下沉操作；维护 heap 与 heapIdx 同步
template <typename Compare>
void sift_down(std::vector<double>& heap,
               std::vector<int>& heap_idx,
               int start, int end,
               Compare cmp) {
    int root = start;
    while (2 * root + 1 < end) {
        int child = 2 * root + 1;
        if (child + 1 < end && cmp(heap[child + 1], heap[child])) {
            ++child;
        }
        if (!cmp(heap[child], heap[root])) break;
        std::swap(heap[root], heap[child]);
        std::swap(heap_idx[root], heap_idx[child]);
        root = child;
    }
}

// 小顶堆的上浮操作
template <typename Compare>
void sift_up(std::vector<double>& heap,
             std::vector<int>& heap_idx,
             int j,
             Compare cmp) {
    while (j > 0) {
        int parent = (j - 1) / 2;
        if (!cmp(heap[j], heap[parent])) break;
        std::swap(heap[j], heap[parent]);
        std::swap(heap_idx[j], heap_idx[parent]);
        j = parent;
    }
}

} // anonymous namespace

RollingTopKResult rolling_topk(const Eigen::VectorXd& values,
                                int k,
                                bool desc) {
    RollingTopKResult result;
    const int n = static_cast<int>(values.size());
    if (n <= 0 || k <= 0) {
        result._values = Eigen::VectorXd(0);
        result._indices = Eigen::VectorXi(0);
        return result;
    }
    k = std::min(k, n);

    // 小顶堆：堆顶是 top-K 中最小的元素（用于比较新元素是否入选）
    std::vector<double> heap;
    std::vector<int> heap_idx;
    heap.reserve(k);
    heap_idx.reserve(k);

    for (int i = 0; i < n; ++i) {
        double v = values[i];
        if (static_cast<int>(heap.size()) < k) {
            heap.push_back(v);
            heap_idx.push_back(i);
            // sift up: 小顶堆，父节点 <= 子节点
            sift_up(heap, heap_idx, static_cast<int>(heap.size()) - 1,
                    std::less<double>());
        } else if (v > heap[0]) {
            heap[0] = v;
            heap_idx[0] = i;
            sift_down(heap, heap_idx, 0, k, std::less<double>());
        }
        // else: v <= 堆顶，跳过（小顶堆堆顶 = top-K 最小值）
    }

    // 堆内是未排序的 top-K，按值降/升序排列最终结果
    std::vector<int> order(k);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) {
                  if (desc) return heap[a] > heap[b];
                  else      return heap[a] < heap[b];
              });

    result._values.resize(k);
    result._indices.resize(k);
    for (int i = 0; i < k; ++i) {
        result._values(i)  = heap[order[i]];
        result._indices(i) = heap_idx[order[i]];
    }
    return result;
}

RollingTopKResult rolling_topk(const std::vector<double>& values,
                                int k,
                                bool desc) {
    Eigen::VectorXd eig(values.size());
    for (size_t i = 0; i < values.size(); ++i) eig(i) = values[i];
    return rolling_topk(eig, k, desc);
}

} // namespace Alg
