/**
 * AgentRouter — 主进程 LLM 调用路由中心
 *
 * 所有 Agent 实例注册到此 Router，renderer 通过 IPC dispatch 请求。
 * 为未来多 Agent 架构（ChatAgent、MonitorAgent 等）铺路。
 */

export interface Agent {
  name: string
  run(input: string, options?: Record<string, any>): Promise<any>
}

export class AgentRouter {
  private agents: Map<string, Agent> = new Map()

  register(name: string, agent: Agent): void {
    this.agents.set(name, agent)
    console.log(`[AgentRouter] registered agent: ${name}`)
  }

  async dispatch(name: string, input: string, options?: Record<string, any>): Promise<any> {
    const agent = this.agents.get(name)
    if (!agent) {
      throw new Error(`Agent "${name}" not found. Registered: ${Array.from(this.agents.keys()).join(', ')}`)
    }
    return agent.run(input, options)
  }

  listAgents(): string[] {
    return Array.from(this.agents.keys())
  }
}

// 单例，供 main.ts 统一使用
export const agentRouter = new AgentRouter()
