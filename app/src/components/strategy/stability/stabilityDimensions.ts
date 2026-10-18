// app/src/components/strategy/stability/stabilityDimensions.ts
// 稳定性分析的扰动维度声明
//
// 稳定性 = 抗扰动能力：输入抖一下、初始值变一下，策略的收益能力会不会直接崩掉，
// 样本外会不会天差地别。每个维度的最终输出形态都是同一套：
// N 次运行 × M 个指标 → 敏感性曲线 + 崩溃阈值 + 红/黄/绿判定。
//
// source 决定实现归属：
//   'frontend' —— 改请求里的 script（capital / backtest.start-end / 节点 params）即可，
//                 前端循环调用现有 POST /v0/backtest 就能跑，不需要新接口
//   'backend'  —— 请求体无法表达该扰动，必须由 C++ 端提供能力

export type PerturbationSource = 'frontend' | 'backend'

export interface PerturbationFactor {
  /** 扰动因子 id */
  key: string
  /** 因子显示名 */
  label: string
  /** 默认取值（文本，用于配置区输入框预填） */
  defaultValue: string
  /** 该因子具体怎么扰动 */
  note?: string
}

export interface StabilityDimension {
  id: string
  /** 维度显示名 */
  label: string
  /** Font Awesome 图标类 */
  icon: string
  source: PerturbationSource
  /** 一句话说明这个维度回答什么问题 */
  summary: string
  /** source 为 backend 时，说明 C++ 需要提供什么 */
  backendNeed?: string
  /** 扰动因子（维度不再细分子检验时直接列在这里） */
  factors: PerturbationFactor[]
  /** 每次运行需要采集的指标 */
  metrics: string[]
  /** 红/黄/绿判定标准 */
  verdict: string
  /** 子检验：同一维度内可以分头落地、各自标注是否已接入 */
  subChecks?: StabilitySubCheck[]
}

export interface StabilitySubCheck {
  id: string
  label: string
  /** 前端执行逻辑是否已接通 */
  implemented: boolean
  /** 未接入时说明还缺什么 */
  pendingNote?: string
  factors: PerturbationFactor[]
  metrics: string[]
  verdict: string
}

export const STABILITY_DIMENSIONS: StabilityDimension[] = [
  {
    id: 'noise',
    label: '行情噪声扰动',
    icon: 'fas fa-random',
    source: 'backend',
    summary: '给行情加噪声后策略还赚不赚 —— 最直接检验信号是否依赖精确价格。',
    backendNeed:
      '回测请求需支持行情噪声注入（如 perturbation: { type: "noise", sigma, seed }），当前请求体无法表达该扰动。',
    factors: [
      {
        key: 'sigma',
        label: '噪声幅度 σ',
        defaultValue: '0.001, 0.003, 0.005, 0.01, 0.02',
        note: '对 bar 价格注入的高斯噪声标准差，按比例计',
      },
      {
        key: 'noiseType',
        label: '噪声类型',
        defaultValue: '高斯加性',
        note: '高斯加性 / 乘性对数，需后端支持',
      },
      {
        key: 'seed',
        label: '随机种子',
        defaultValue: '42',
        note: '同种子必须可复现，否则该维度无法判定',
      },
      {
        key: 'repeats',
        label: '每档重复次数',
        defaultValue: '5',
        note: '单次结果可能是运气，需要重复取分布',
      },
    ],
    metrics: ['总收益率', '年化收益率', '夏普比率', '最大回撤', '交易次数'],
    verdict: 'σ 增大到 1% 时总收益仍为正、夏普不低于基准的 60% → 绿；总收益转负 → 红。',
  },
  {
    id: 'initial',
    label: '初始值扰动',
    icon: 'fas fa-coins',
    source: 'frontend',
    summary:
      '换初始资金、换回测起点后收益是否还在 —— 资金经下单量传导到滑点与冲击成本，起点偏移检验收益是否只吃某一段行情。',
    factors: [],
    metrics: [],
    verdict: '见下方子检验。',
    subChecks: [
      {
        id: 'capital',
        label: '资金规模不变性',
        implemented: true,
        factors: [
          {
            key: 'capitalTiers',
            label: '资金档位',
            defaultValue: '100000, 300000, 1000000, 3000000, 10000000',
            note: '按资金池 totalAvailable 上限过滤；标准档位不足 2 个时退化为按上限比例生成',
          },
        ],
        metrics: [
          '各档资金年化收益率 R(c)',
          '归一化收益曲线 R(c)/R(c_base)',
          '资金敏感性 ε=(max R − min R)/|median R|',
          '换手率 Σ成交金额/c',
          '笔数比值 N(c)/N(c_base)',
        ],
        verdict:
          '预期形状由滑点模型决定：固定比例模型应平坦（ε<0.3 → 绿），成交量冲击模型应随 c^alpha 单调递减；任一档收益转负 → 红。滑点未启用时结论只能是「未检验」。',
      },
      {
        id: 'startShift',
        label: '回测起点不变性',
        implemented: true,
        factors: [
          {
            key: 'startShift',
            label: '起点偏移步长',
            defaultValue: '21 bar（约 1 个月）',
            note: '映射到 script.backtest.start，end 固定不动',
          },
          {
            key: 'startTailGuard',
            label: '起点扫描尾部保护',
            defaultValue: '末尾保留 126 bar',
            note: '样本过短的起点不可比，该区间的起点不参与判定',
          },
        ],
        metrics: [
          '各起点年化收益率 r_i',
          '起点收益极差与最大相邻跳变',
          '收益对起点的 lag-1 自相关',
          '置换检验 p 值（时间结构显著性）',
          'r ~ 起点序号 趋势斜率',
          '负收益起点占比',
          '断崖数（孤立跳变且收益符号翻转）',
        ],
        verdict:
          '变化随机且幅度小 → 绿，系统性漂移但连续 → 黄，存在收益符号翻转的孤立跳变（断崖）或过半起点收益为负 → 红。比较必须用年化收益，否则样本长度递减会伪造「起点越晚越差」。基准回归 R² 未纳入：基准行情走外部 TickFlow API，会引入额外失败点。',
      },
    ],
  },
  {
    id: 'param',
    label: '参数扰动',
    icon: 'fas fa-sliders-h',
    source: 'frontend',
    summary: '参数稍微一动就崩，说明收益是拟合出来的而不是结构性的。',
    factors: [
      {
        key: 'targets',
        label: '扫描参数',
        defaultValue: '自动提取 params[type=number] 的节点参数',
        note: 'select/boolean 值为枚举，text/date 非数值，都不纳入扫描',
      },
      {
        key: 'gridSize',
        label: '每参数网格点数',
        defaultValue: '5（对称，不含基准）',
        note: '步长取参数自带的 step，缺失时退化为 value × 10%',
      },
    ],
    metrics: [
      '各档位年化收益率',
      '邻域正收益占比',
      '基准落差 (R(基准) − max(R(扰动)))/|R(基准)|',
      '最差档位',
      '夏普 / 最大回撤 / 笔数',
    ],
    verdict:
      '邻域正收益占比 < 50% → 红（参数一动就崩，典型过拟合）；< 80% → 黄；≥ 80% 但基准落差 > 50% → 黄（参数处在刃口）；否则绿。整体判定取最脆弱的那个参数。',
  },
  {
    id: 'walkforward',
    label: '样本外一致性',
    icon: 'fas fa-forward',
    source: 'backend',
    summary: '滚动切分训练/测试，逐段样本外收益的方向是否一致 —— 过拟合最直接的证据。',
    backendNeed:
      '需要训练/测试切分管线：ML 节点按训练段重新训练，在测试段推理，再逐段拼接样本外收益。',
    factors: [
      {
        key: 'trainLen',
        label: '训练窗长度',
        defaultValue: '252',
        note: 'bar 数',
      },
      {
        key: 'testLen',
        label: '测试窗长度',
        defaultValue: '63',
        note: 'bar 数',
      },
      {
        key: 'step',
        label: '滚动步长',
        defaultValue: '63',
        note: '每次前移的 bar 数',
      },
      {
        key: 'anchor',
        label: '锚定方式',
        defaultValue: '滚动',
        note: '滚动窗口 / 扩展窗口，需后端支持',
      },
    ],
    metrics: [
      '各段样本外收益',
      '样本外正收益段占比',
      '段间收益离散度 (CV)',
      '拼接曲线 vs 全样本曲线偏离度',
    ],
    verdict: '正收益段占比 ≥ 60% 且段间 CV < 0.5 → 绿；正收益段不足一半 → 红。',
  },
]

export const SOURCE_LABEL: Record<PerturbationSource, string> = {
  frontend: '前端可实现',
  backend: '需后端支持',
}
