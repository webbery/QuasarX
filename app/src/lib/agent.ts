/**
 * Agent 模型配置管理工具
 * 用于管理大语言模型的 URL、协议和 API Key
 */

import Anthropic from '@anthropic-ai/sdk'

export interface AgentConfig {
  url: string;          // 模型服务 URL，例如：https://api.openai.com
  protocol: string;     // 协议类型：openai / anthropic / custom
  apiKey: string;       // API Key
  model: string;     // 模型名
}

const STORAGE_KEY = 'quasarx_agent_config';

/**
 * 获取 Agent 配置
 */
export function getAgentConfig(): AgentConfig | null {
  try {
    const stored = localStorage.getItem(STORAGE_KEY);
    if (stored) {
      return JSON.parse(stored);
    }
  } catch (e) {
    console.error('Failed to parse agent config:', e);
  }
  return null;
}

/**
 * 保存 Agent 配置
 */
export function saveAgentConfig(config: AgentConfig): void {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(config));
}

/**
 * 删除 Agent 配置
 */
export function removeAgentConfig(): void {
  localStorage.removeItem(STORAGE_KEY);
}

/**
 * 测试 Agent 连接
 * 发送一个简单的请求来验证配置是否正确
 */
export async function testAgentConnection(config: AgentConfig): Promise<{ success: boolean; message: string }> {
  try {
    if (!config.url) {
      return { success: false, message: 'URL 不能为空' };
    }
    if (!config.apiKey) {
      return { success: false, message: 'API Key 不能为空' };
    }

    // 构建测试请求
    let testUrl: string;
    let headers: Record<string, string>;
    let body: object;

    switch (config.protocol) {
      case 'openai':
        testUrl = `${config.url}/v1/chat/completions`;
        headers = {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${config.apiKey}`
        };
        body = {
          model: config.model,
          messages: [{ role: 'user', content: 'Hi' }],
          max_tokens: 10
        };
        break;
      
      case 'anthropic': {
        const anthropic = new Anthropic({
          apiKey: config.apiKey,
          baseURL: config.url,
          dangerouslyAllowBrowser: true
        });
        const response = await anthropic.messages.create({
          model: config.model,
          messages: [{ role: 'user', content: 'Hi' }],
          max_tokens: 10
        });
        const hasValidContent = response.content?.some(block => ['text', 'thinking', 'reasoning'].includes(block.type));
        if (hasValidContent) {
          return { success: true, message: '连接成功，模型服务可用' };
        }
        return { success: false, message: '连接失败: 未收到有效回复' };
      }

      default:
        // 自定义协议，使用 OpenAI 兼容格式
        testUrl = `${config.url}/v1/chat/completions`;
        headers = {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${config.apiKey}`
        };
        body = {
          messages: [{ role: 'user', content: 'Hi' }],
          max_tokens: 10
        };
    }

    const response = await fetch(testUrl, {
      method: 'POST',
      headers,
      body: JSON.stringify(body)
    });

    if (response.ok) {
      return { success: true, message: '连接成功，模型服务可用' };
    } else {
      const errorData = await response.json().catch(() => null);
      const errorMsg = errorData?.error?.message || response.statusText;
      return { success: false, message: `连接失败: ${errorMsg} (${response.status})` };
    }
  } catch (e) {
    const errorMessage = e instanceof Error ? e.message : String(e)
    return { success: false, message: `连接失败: ${errorMessage}` };
  }
}

/**
 * 使用 Agent 发送消息
 * @param message 用户消息
 * @param systemPrompt 系统提示词（可选）
 * @returns AI 回复
 */
export async function sendAgentMessage(
  message: string,
  systemPrompt?: string
): Promise<string> {
  const config = getAgentConfig();
  if (!config) {
    throw new Error('Agent 未配置，请先在设置中配置');
  }

  const messages: Array<{ role: string; content: string }> = [];
  
  if (systemPrompt) {
    messages.push({ role: 'system', content: systemPrompt });
  }
  messages.push({ role: 'user', content: message });

  let url: string;
  let headers: Record<string, string>;
  let body: object;

  switch (config.protocol) {
    case 'openai':
      url = `${config.url}/v1/chat/completions`;
      headers = {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${config.apiKey}`
      };
      body = {
        model: 'gpt-3.5-turbo',
        messages
      };
      break;
    
    case 'anthropic': {
      const anthropic = new Anthropic({
        apiKey: config.apiKey,
        baseURL: config.url,
        dangerouslyAllowBrowser: true
      });
      const anthropicMessages = messages.filter(m => m.role !== 'system').map(m => ({
        role: m.role as 'user' | 'assistant',
        content: m.content
      }));
      // system prompt 需要单独传递
      const systemPrompt = messages.find(m => m.role === 'system')?.content;
      const response = await anthropic.messages.create({
        model: config.model,
        messages: anthropicMessages,
        max_tokens: 4096,
        system: systemPrompt
      });
      const text = response.content?.find(block => block.type === 'text');
      return (text as any)?.text || '';
    }

    default:
      url = `${config.url}/v1/chat/completions`;
      headers = {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${config.apiKey}`
      };
      body = { messages };
  }

  const response = await fetch(url, {
    method: 'POST',
    headers,
    body: JSON.stringify(body)
  });

  if (!response.ok) {
    const errorData = await response.json().catch(() => null);
    const errorMsg = errorData?.error?.message || response.statusText;
    throw new Error(`请求失败: ${errorMsg}`);
  }

  const data = await response.json();
  return data.choices?.[0]?.message?.content || '';
}
