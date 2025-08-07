# 转换为TVM模型
import tvm
from tvm import relax
import tensorflow as tf
import numpy as np
import argparse
import json

class TVMConvertor:
    def __init__(self):
        pass

    def convert_onnx(self, onnx_path, output_name, input_shape_dict, target='llvm'):
        '''
        target: llvm/cuda/c 编译目标（根据部署设备选择）
        '''
        # 1. 加载已保存的 TensorFlow 模型
        onnx_model = tf.saved_model.load(onnx_path)

        # 4. 转换为 relax 中间表示
        mod, params = relax.frontend.from_onnx(
            onnx_model,
            shape=input_shape_dict,
        )

        # 6. 编译模型
        with tvm.transform.PassContext(opt_level=3):
            lib = relax.build(mod, target=target, params=params)

        # 7. 保存编译后的模型
        lib.export_library(output_name + '.so')

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-i', "--input", type=str, required=True)
    parser.add_argument('-n', "--export-name", type=str, default='out.bin')
    parser.add_argument('-o', "--target", type=str, default='llvm')
    parser.add_argument('-s',"--input-shapes", type=str, default='{"data": [1, 3, 224, 224]}')
    args = parser.parse_args()

    shape_dict = json.loads(args.input_shapes)
    convertor = TVMConvertor()
    if args.type == 'tf':
        convertor.convert_onnx(args.input, args.export_name, shape_dict, args.target)
    else:
        print('not support type `{}`'.format(args.type))