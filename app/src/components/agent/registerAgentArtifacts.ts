/**
 * 产物渲染器注册
 *
 * 新增一种产物只需在这里加一行 registerArtifact，不用改 AgentArtifactCard。
 */

import { registerArtifact, listArtifactKinds } from '@/lib/agent/artifactRegistry'
import AgentChartRenderer from './AgentChartRenderer.vue'
import AgentMetricArtifact from './AgentMetricArtifact.vue'
import AgentTableArtifact from './AgentTableArtifact.vue'
import AgentPatchArtifact from './AgentPatchArtifact.vue'
import AgentActionArtifact from './AgentActionArtifact.vue'

let registered = false

export function registerAgentArtifacts(): void {
  if (registered) return
  registered = true

  registerArtifact({
    kind: 'chart',
    label: '图表',
    icon: 'chart-line',
    render: AgentChartRenderer,
    compact: true,
    exportable: true,
  })

  registerArtifact({
    kind: 'metric',
    label: '指标',
    icon: 'gauge',
    render: AgentMetricArtifact,
  })

  registerArtifact({
    kind: 'table',
    label: '表格',
    icon: 'table',
    render: AgentTableArtifact,
    exportable: true,
  })

  registerArtifact({
    kind: 'patch',
    label: '策略修改提案',
    icon: 'code-branch',
    render: AgentPatchArtifact,
  })

  registerArtifact({
    kind: 'action',
    label: '操作',
    icon: 'bolt',
    render: AgentActionArtifact,
  })

  console.info('[AgentArtifacts] 已注册产物类型:', listArtifactKinds().join(', '))
}
