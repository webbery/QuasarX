#include "Interprecter/Stmt.h"

// ========== CrossSectionGraph 实现 ==========

CrossSectionNode& CrossSectionGraph::addNode(const String& id) {
    auto& node = nodes[id];
    node.id = id;
    return node;
}

void CrossSectionGraph::addEdge(const String& from, const String& to) {
    nodes[from].dependencies.push_back(to);
    nodes[to].inDegree++;
}

bool CrossSectionGraph::topologicalSort() {
    evalOrder.clear();
    Queue<String> queue;

    // 将所有入度为 0 的节点加入队列
    for (auto& [id, node] : nodes) {
        if (node.inDegree == 0) {
            queue.push(id);
        }
    }

    while (!queue.empty()) {
        String current = queue.front();
        queue.pop();
        evalOrder.push_back(current);

        // 减少依赖节点的入度
        for (auto& depId : nodes[current].dependencies) {
            auto it = nodes.find(depId);
            if (it != nodes.end()) {
                it->second.inDegree--;
                if (it->second.inDegree == 0) {
                    queue.push(depId);
                }
            }
        }
    }

    // 如果所有节点都被访问，说明无环
    return evalOrder.size() == nodes.size();
}

void CrossSectionGraph::clear() {
    nodes.clear();
    evalOrder.clear();
}
