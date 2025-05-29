import os
import subprocess
import glob

def convert_mp4_to_avi():
    # 获取当前目录下所有的mp4文件
    mp4_files = glob.glob("*.mp4")
    
    if not mp4_files:
        print("当前目录下没有找到MP4文件")
        return
    
    print(f"找到 {len(mp4_files)} 个MP4文件，开始转换...")
    
    for mp4_file in mp4_files:
        # 生成输出文件名（替换扩展名）
        avi_file = os.path.splitext(mp4_file)[0] + ".avi"
        
        # 构建ffmpeg命令
        cmd = [
            "ffmpeg",
            "-i", mp4_file,
            "-vcodec", "mjpeg",
            "-vf", "scale=360:360",
            "-r", "15",
            "-q:v", "6",
            "-acodec", "pcm_s16le",
            "-ar", "16000",
            avi_file
        ]
        
        print(f"正在转换: {mp4_file} -> {avi_file}")
        
        try:
            # 执行ffmpeg命令
            subprocess.run(cmd, check=True)
            print(f"转换成功: {avi_file}")
        except subprocess.CalledProcessError as e:
            print(f"转换失败: {mp4_file}, 错误: {str(e)}")
        except Exception as e:
            print(f"发生错误: {str(e)}")

if __name__ == "__main__":
    print("开始批量转换MP4文件为AVI格式...")
    convert_mp4_to_avi()
    print("转换完成！") 