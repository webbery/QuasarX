# Tool 插件目录

将自定义 Tool 放在此目录下，系统会自动加载。

## 创建自定义 Tool

在 `plugins/` 目录创建 `.ts` 文件，例如 `plugins/myTool.ts`：

```typescript
import { tool } from "@langchain/core/tools"
import { z } from "zod"

export default tool(
  async (input) => {
    // 你的逻辑
    return `结果: ${JSON.stringify(input)}`
  },
  {
    name: "myTool",
    description: "我的自定义工具",
    schema: z.object({
      // 定义参数 schema
    }),
  }
)
```

## 注意事项

- 文件必须使用 `.ts` 扩展
- 默认导出必须是 `StructuredToolInterface` 或数组
- 插件 Tool 在 `getAllTools()` 调用时自动加载
