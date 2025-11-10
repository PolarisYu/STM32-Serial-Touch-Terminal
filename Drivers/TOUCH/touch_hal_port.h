#ifndef __TOUCH_HAL_PORT_H
#define __TOUCH_HAL_PORT_H

#include <stdint.h>

/*
================================================================================
  硬件抽象层 (HAL) 端口 - 触摸屏
  
  您必须为您的特定平台实现此文件中的所有 "TOUCH_HW_..." 函数。
================================================================================
*/


/*
================================================================================
  1. GPIO (RST, INT)
================================================================================
*/

/**
 * @brief 初始化 RST (输出) 和 INT (输入) 引脚
 */
void TOUCH_HW_GPIO_Init(void);

/**
 * @brief 设置 RST 引脚电平
 * @param state: 0 = 低电平, 1 = 高电平
 */
void TOUCH_HW_Set_RST(uint8_t state);

/**
 * @brief 读取 INT 引脚电平
 * @return 0 = 低电平, 1 = 高电平
 */
uint8_t TOUCH_HW_Read_INT(void);

void TOUCH_HW_Set_INT_Mode_Output(void);



/*
================================================================================
  2. 软件 I2C (基于 ctiic.c)
================================================================================
*/

/**
 * @brief 初始化 I2C (SCL, SDA) 引脚
 */
void TOUCH_HW_IIC_Init(void);

/**
 * @brief 发送 I2C 开始信号
 */
void TOUCH_HW_IIC_Start(void);

/**
 * @brief 发送 I2C 停止信号
 */
void TOUCH_HW_IIC_Stop(void);

/**
 * @brief I2C 发送一个字节
 * @param txd: 要发送的字节
 */
void TOUCH_HW_IIC_Send_Byte(uint8_t txd);

/**
 * @brief I2C 读取一个字节
 * @param ack: 1 = 发送 ACK, 0 = 发送 NACK
 * @return 读取到的字节
 */
uint8_t TOUCH_HW_IIC_Read_Byte(unsigned char ack);

/**
 * @brief I2C 等待 ACK 信号
 * @return 0: 收到ACK, 1: 未收到ACK
 */
uint8_t TOUCH_HW_IIC_Wait_Ack(void);

/**
 * @brief I2C 发送 ACK 信号
 */
void TOUCH_HW_IIC_Ack(void);

/**
 * @brief I2C 发送 NACK 信号
 */
void TOUCH_HW_IIC_NAck(void);


/*
================================================================================
  3. 延时
================================================================================
*/

/**
 * @brief 毫秒延时
 */
void TOUCH_HW_Delay_ms(uint32_t ms);

/**
 * @brief 微秒延时
 */
void TOUCH_HW_Delay_us(uint32_t us);


#endif