import time
import random
import requests

# Wio Terminal的IP地址，假设它已经连接到同一Wi-Fi网络
wio_ip = "http://192.168.188.107"  # 请将此替换为Wio Terminal的实际IP地址

# 生成一个随机股价
def generate_random_price():
    return round(random.uniform(100.0, 200.0), 2)  # 随机生成100到200之间的小数，保留两位小数

# 推送股价数据到Wio Terminal
def push_stock_price():
    while True:
        # 生成随机股价
        stock_price = generate_random_price()

        # 发送HTTP请求，将股价数据传递给Wio Terminal
        try:
            response = requests.get(f"{wio_ip}/?stock_price={stock_price}")
            if response.status_code == 200:
                print(f"Successfully sent stock price {stock_price} to Wio Terminal")
            else:
                print(f"Failed to send data. Status code: {response.status_code}")
        except requests.exceptions.RequestException as e:
            print(f"Error sending data: {e}")

        # 每隔10秒发送一次股价数据
        time.sleep(0.5)

if __name__ == "__main__":
    push_stock_price()
