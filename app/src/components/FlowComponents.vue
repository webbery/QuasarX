<template>
    <div class="components-panel">
        <div class="panel-title">机器学习组件</div>
        <div class="components-list">
            <div class="component-card" draggable="true" @dragstart="onDragStart($event, {type: 'linearRegression', label: '线性回归'})">
                <div class="component-header">
                    <div class="component-icon" style="background-color: #10b981;">
                        <i class="fas fa-chart-line"></i>
                    </div>
                    <div class="component-title">线性回归</div>
                </div>
                <div class="component-desc">用于预测连续值的线性模型</div>
            </div>
        </div>
    </div>
</template>
<script setup>

</script>
<style scoped>
.components-panel {
    width: 280px;
    background-color: white;
    border-left: 1px solid #e0e0e0;
    padding: 15px;
    overflow-y: auto;
    box-shadow: -2px 0 5px rgba(0, 0, 0, 0.05);
    margin-top: 60px;
}
.panel-title {
    font-size: 1.2rem;
    margin-bottom: 15px;
    color: #2c3e50;
    font-weight: 600;
    padding-bottom: 10px;
    border-bottom: 2px solid #eee;
}
</style>