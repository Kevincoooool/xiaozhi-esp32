# 批量转换音频文件到 protocol v3 stream
import os
import sys
import argparse
from pathlib import Path
from convert_audio_to_p3 import encode_audio_to_opus

def batch_convert(input_dir, output_dir, target_lufs=None, file_ext='.mp3'):
    """批量转换指定目录下的所有音频文件为P3格式"""
    # 确保输出目录存在
    os.makedirs(output_dir, exist_ok=True)
    
    # 获取所有指定扩展名的文件
    input_files = list(Path(input_dir).glob(f'*{file_ext}'))
    
    if not input_files:
        print(f"在 {input_dir} 中没有找到 {file_ext} 文件")
        return
    
    print(f"找到 {len(input_files)} 个{file_ext}文件，开始转换...")
    
    # 逐个转换文件
    for input_file in input_files:
        # 保持文件名，只改变扩展名和路径
        output_file = Path(output_dir) / (input_file.stem + '.p3')
        
        print(f"正在转换: {input_file.name} -> {output_file.name}")
        try:
            encode_audio_to_opus(str(input_file), str(output_file), target_lufs)
            print(f"转换成功: {output_file.name}")
        except Exception as e:
            print(f"转换失败 {input_file.name}: {str(e)}")
    
    print("批量转换完成!")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='批量转换音频文件为P3格式')
    parser.add_argument('input_dir', help='输入音频文件目录')
    parser.add_argument('output_dir', help='输出P3文件目录')
    parser.add_argument('-l', '--lufs', type=float, default=-16.0,
                       help='目标响度LUFS值 (默认: -16)')
    parser.add_argument('-d', '--disable-loudnorm', action='store_true',
                       help='禁用响度标准化')
    parser.add_argument('-e', '--extension', default='.mp3',
                       help='要处理的文件扩展名 (默认: .mp3)')
    args = parser.parse_args()

    target_lufs = None if args.disable_loudnorm else args.lufs
    batch_convert(args.input_dir, args.output_dir, target_lufs, args.extension)