<template>
          <!-- 策略表现 -->
      <div class="card">
        <div class="section-title">
          <h2><i class="fas fa-project-diagram"></i> 策略表现分析</h2>
          <div>
            <select class="control-btn">
              <option>Alpha策略</option>
              <option selected>市场中性策略</option>
              <option>CTA趋势策略</option>
              <option>套利策略</option>
            </select>
          </div>
        </div>
        <div class="chart-container" id="strategyChart"></div>
      </div>
</template>