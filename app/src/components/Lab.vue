<script setup lang="ts">
defineOptions({
    name: 'LabVue'
})
</script>

<template>
    <!-- 压力测试面板 -->
      <div class="card">
        <div class="section-title">
          <h2><i class="fas fa-wind"></i> 压力测试实验室</h2>
          <div class="tabs">
            <div class="tab active">蒙特卡罗模拟</div>
            <div class="tab">有限差分法</div>
            <div class="tab">极端场景</div>
          </div>
        </div>

        <div class="param-controls">
          <div class="param-group">
            <div class="param-title">
              <i class="fas fa-chart-line"></i> 波动率参数
            </div>
            <div class="slider-container">
              <input
                type="range"
                min="0"
                max="200"
                value="30"
                class="slider"
                id="volatility"
              />
              <div class="slider-value">30%</div>
            </div>
            <div class="slider-container">
              <input
                type="range"
                min="0"
                max="500"
                value="120"
                class="slider"
                id="volatilityShock"
              />
              <div class="slider-value">120% (黑天鹅冲击)</div>
            </div>
          </div>

          <div class="param-group">
            <div class="param-title">
              <i class="fas fa-exchange-alt"></i> 流动性参数
            </div>
            <div class="slider-container">
              <input
                type="range"
                min="0"
                max="300"
                value="100"
                class="slider"
                id="spread"
              />
              <div class="slider-value">100% (当前水平)</div>
            </div>
            <div class="slider-container">
              <input
                type="range"
                min="0"
                max="1000"
                value="500"
                class="slider"
                id="spreadShock"
              />
              <div class="slider-value">500% (流动性危机)</div>
            </div>
          </div>

          <div class="param-group">
            <div class="param-title">
              <i class="fas fa-link"></i> 相关性冲击
            </div>
            <div class="slider-container">
              <input
                type="range"
                min="0"
                max="100"
                value="35"
                class="slider"
                id="correlation"
              />
              <div class="slider-value">相关系数: 0.35</div>
            </div>
            <div class="slider-container">
              <input
                type="range"
                min="0"
                max="100"
                value="85"
                class="slider"
                id="correlationShock"
              />
              <div class="slider-value">危机相关系数: 0.85</div>
            </div>
          </div>
        </div>

        <div class="btn-group">
          <button class="btn btn-primary">
            <i class="fas fa-bolt"></i> 执行压力测试
          </button>
          <button class="btn btn-outline">
            <i class="fas fa-history"></i> 回测历史场景
          </button>
        </div>

        <div class="chart-container" id="monteCarloChart"></div>
      </div>
</template>