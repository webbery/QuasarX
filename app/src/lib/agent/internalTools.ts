/**
 * 内部 Tool 定义
 *
 * 内部 Tool 用于 Agent 间的通信。当一个 Agent 调用 ask_xxx Tool 时，
 * 实际上是触发了对应 Agent Node 的执行。
 *
 * 注意：这些 Tool 的 invoke 并不直接执行 Agent 逻辑，而是返回一个标记，
 * 由 LangGraph 的状态机在后续步骤中路由到对应 Agent。
 *
 * 实际执行方式：在 Supervisor 图中，这些 Tool 被注册为"占位符"，
 * 当检测到调用 ask_xxx 时，Supervisor 自动路由到对应 Agent。
 */

import { z } from "zod";
import { tool } from "@langchain/core/tools";

/** ask_chat: 请求 Chat Agent 进行通用问答 */
export const askChatTool = tool(
  async (input: { query: string }) => {
    // 这个 Tool 在 LangGraph 图中会被 Supervisor 拦截并路由到 Chat Agent
    // 返回值只是一个 fallback，实际不会直接调用到这里
    return `[Chat Agent 回复]: ${input.query}`;
  },
  {
    name: "ask_chat",
    description: "请求 Chat Agent 进行一般性问答，包括知识检索、网络搜索、日期时间查询等。当需要通用知识时使用此工具。",
    schema: z.object({
      query: z.string().describe("要问 Chat Agent 的问题"),
    }),
  },
);

/** ask_strategy: 请求 Strategy Agent 处理策略相关任务 */
export const askStrategyTool = tool(
  async (input: { query: string }) => {
    return `[Strategy Agent 回复]: ${input.query}`;
  },
  {
    name: "ask_strategy",
    description: "请求 Strategy Agent 处理策略创建、优化、回测等任务。当需要创建策略、执行回测或分析策略表现时使用此工具。",
    schema: z.object({
      query: z.string().describe("要问 Strategy Agent 的问题"),
    }),
  },
);

/** ask_risk: 请求 Risk Agent 进行风控分析 */
export const askRiskTool = tool(
  async (input: { query: string }) => {
    return `[Risk Agent 回复]: ${input.query}`;
  },
  {
    name: "ask_risk",
    description: "请求 Risk Agent 进行风控分析，包括账户风险评估、持仓风险、突变检测等。当需要风控建议或风险检查时使用此工具。",
    schema: z.object({
      query: z.string().describe("要问 Risk Agent 的问题"),
    }),
  },
);

/** ask_portfolio: 请求 Portfolio Agent 分析投资组合 */
export const askPortfolioTool = tool(
  async (input: { query: string }) => {
    return `[Portfolio Agent 回复]: ${input.query}`;
  },
  {
    name: "ask_portfolio",
    description: "请求 Portfolio Agent 分析投资组合，包括组合收益、资产配置建议等。当需要投资组合分析或配置建议时使用此工具。",
    schema: z.object({
      query: z.string().describe("要问 Portfolio Agent 的问题"),
    }),
  },
);

/** 所有内部 Tool 列表 */
export const internalTools = [
  askChatTool,
  askStrategyTool,
  askRiskTool,
  askPortfolioTool,
];

/** 内部 Tool 名称到 Agent 类型的映射（用于 Supervisor 路由） */
export const INTERNAL_TOOL_AGENT_MAP: Record<string, string> = {
  ask_chat: "chat",
  ask_strategy: "strategy",
  ask_risk: "risk",
  ask_portfolio: "portfolio",
};
