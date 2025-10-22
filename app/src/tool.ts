import { createApp, type App } from 'vue'
import MessageTips from './components/MessageTips.vue';

const padZero = (num: Number) => {
  return num.toString().padStart(2, '0');
};

// 消息配置接口
interface MessageOptions {
  type?: 'success' | 'info' | 'warning' | 'error'
  duration?: number
  closable?: boolean
}

// 创建消息应用
let messageApp: App<Element> | null = null
let messageInstance: any = null

// 初始化消息组件
const initMessage = () => {
  if (!messageApp) {
    const div = document.createElement('div')
    document.body.appendChild(div)
    
    messageApp = createApp(MessageTips)
    messageInstance = messageApp.mount(div)
  }
}

// 显示消息的函数
export const message = {
  show: (content: string, options: MessageOptions = {}) => {
    if (!messageInstance) {
      initMessage()
    }
    
    const message = {
      id: Date.now() + Math.random(),
      content,
      type: options.type || 'info',
      duration: options.duration ?? 5500,
      closable: options.closable ?? true
    }
    
    messageInstance.addMessage(message)
  },
  
  success: (content: string, duration?: number) => {
    message.show(content, { type: 'success', duration })
  },
  
  info: (content: string, duration?: number) => {
    message.show(content, { type: 'info', duration })
  },
  
  warning: (content: string, duration?: number) => {
    message.show(content, { type: 'warning', duration })
  },
  
  error: (content: string, duration?: number) => {
    message.show(content, { type: 'error', duration })
  }
}

export const formatDate = (date: Date) => {
    const year = date.getFullYear();
    const month = padZero(date.getMonth() + 1); // 月份从 0 开始，需 +1
    const day = padZero(date.getDate());
    const hours = padZero(date.getHours());
    const minutes = padZero(date.getMinutes());
    const seconds = padZero(date.getSeconds());
    return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
};