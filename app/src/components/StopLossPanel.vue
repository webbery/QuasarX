<script setup lang="ts">
interface StopLossInfo {
  id: number
  name: string
  email: string
  symbol: string
  type: string
  price: number
  stop_price: number
}

let tableData: StopLossInfo[] = [
  {
    id: 12,
    symbol: '100136',
    email: '15600109607@wo.cn',
    name: 'Aleyna Kutzner',
    type: 'A股',
    price: 43.20,
    stop_price: 40.13
  }
]

const deleteRow = (index: number) => {
  // tableData.value.splice(index, 1)
}
</script>
<template>
<div>
  <div>
    <el-input placeholder="代码" style="display: inline;"></el-input>
    <el-input placeholder="报警通知email地址" style="display: inline;"></el-input>
    <el-input placeholder="买入价格" style="display: inline;"></el-input>
    <el-input placeholder="止损比例,如5%" style="display: inline;"></el-input>
    <el-button type="primary" >添加</el-button>
  </div>
    <el-table :data="tableData" style="width: 100%">
      <el-table-column property="symbol" label="代码" width="100" >
        <template #default="scope">{{ scope.row.symbol }}</template>
      </el-table-column>
      <el-table-column label="类型" width="80" >
        <template #default="scope">{{ scope.row.type }}</template>
      </el-table-column>
      <el-table-column property="name" label="名称" width="120" >
          <template #default="scope">{{ scope.row.name }}</template>
        </el-table-column>
      <el-table-column property="price" label="买入价格" width="100" >
          <template #default="scope">{{ scope.row.price }}</template>
      </el-table-column>
      <el-table-column property="price" label="当前价格" width="100" >
          -
      </el-table-column>
      <el-table-column property="stop_price" label="止损价格"  width="100">
          <template #default="scope">{{ scope.row.stop_price }}</template>
        </el-table-column>
        <el-table-column label="EMail" >
          <template #default="scope">{{ scope.row.email }}</template>
      </el-table-column>
      <el-table-column fixed="right" label="Operations" min-width="120">
        <template #default="scope">
          <el-button
            link
            type="primary"
            size="small"
            @click.prevent="deleteRow(scope.$index)"
          >
            删除
          </el-button>
        </template>
      </el-table-column>
    </el-table>
</div>
</template>