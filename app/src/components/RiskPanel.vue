<template>
    <div class="panel-section">
        <div class="panel-title">
          <h3><i class="fas fa-radiation"></i> 风险指标</h3>
          <span>当前持仓</span>
        </div>

        <div class="stats-grid">
          <div class="stat-item">
            <div class="stat-title">Delta 暴露</div>
            <div class="stat-value">0.42</div>
            <div>股票多头</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">Gamma 暴露</div>
            <div class="stat-value">0.08</div>
            <div>期权持仓</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">Vega 暴露</div>
            <div class="stat-value">-0.15</div>
            <div>波动率空头</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">Beta 加权</div>
            <div class="stat-value">1.12</div>
            <div>市场敏感度</div>
          </div>
        </div>
      </div>

      <div class="panel-section">
        <div class="panel-title">
          <h3><i class="fas fa-exclamation-triangle"></i> 压力测试结果</h3>
          <span>最新</span>
        </div>

        <div class="stats-grid">
          <div class="stat-item">
            <div class="stat-title">最大回撤</div>
            <div class="stat-value negative">-15.8%</div>
            <div>黑天鹅场景</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">净值下跌</div>
            <div class="stat-value negative">-12.3%</div>
            <div>流动性危机</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">恢复概率</div>
            <div class="stat-value">78%</div>
            <div>6个月内</div>
          </div>
          <div class="stat-item">
            <div class="stat-title">压力指数</div>
            <div class="stat-value negative">7.2/10</div>
            <div>高风险</div>
          </div>
        </div>
      </div>
</template>
<script>

</script>
