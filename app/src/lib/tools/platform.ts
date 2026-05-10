/**
 * 系统信息 Tool
 * 查看系统环境信息
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"

export const platformTool = tool(
  async () => {
    return `【系统环境】\n平台: ${navigator.platform}\n语言: ${navigator.language}\n页面标题: ${document.title}\n屏幕: ${window.screen.width}x${window.screen.height}`
  },
  {
    name: "platform",
    description: "查看系统环境信息（操作系统、浏览器语言、屏幕尺寸等）。",
    schema: z.object({}),
  }
)
