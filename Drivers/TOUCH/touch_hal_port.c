
#include "touch_hal_port.h"
#include "main.h" // 包含 STM32 HAL 库

/*
================================================================================
  引脚定义 (基于您的原始文件)
================================================================================
*/
// I2C
#define CT_SCL_PORT     GPIOB
#define CT_SCL_PIN      GPIO_PIN_0
#define CT_SDA_PORT     GPIOF
#define CT_SDA_PIN      GPIO_PIN_11

// GPIO
#define CT_RST_PORT     GPIOC
#define CT_RST_PIN      GPIO_PIN_13
#define CT_INT_PORT     GPIOB
#define CT_INT_PIN      GPIO_PIN_1

/*
================================================================================
  1. GPIO (RST, INT)
================================================================================
*/

void TOUCH_HW_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_Initure;

    // 使能时钟
    __HAL_RCC_GPIOB_CLK_ENABLE();			
    __HAL_RCC_GPIOC_CLK_ENABLE();			
            
    // INT (PB1) - 上拉输入
    GPIO_Initure.Pin   = CT_INT_PIN;
    GPIO_Initure.Mode  = GPIO_MODE_INPUT;
    GPIO_Initure.Pull  = GPIO_PULLUP;
    GPIO_Initure.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(CT_INT_PORT, &GPIO_Initure);
            
    // RST (PC13) - 推挽输出
    GPIO_Initure.Pin   = CT_RST_PIN;
    GPIO_Initure.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_Initure.Pull  = GPIO_PULLUP; // 保持高电平
    HAL_GPIO_Init(CT_RST_PORT, &GPIO_Initure);
    
    HAL_GPIO_WritePin(CT_RST_PORT, CT_RST_PIN, GPIO_PIN_SET); // 默认释放复位
}

void TOUCH_HW_Set_RST(uint8_t state)
{
    HAL_GPIO_WritePin(CT_RST_PORT, CT_RST_PIN, (state ? GPIO_PIN_SET : GPIO_PIN_RESET));
}

uint8_t TOUCH_HW_Read_INT(void)
{
    return HAL_GPIO_ReadPin(CT_INT_PORT, CT_INT_PIN);
}

void TOUCH_HW_Set_INT_Mode_Output(void)
{
    GPIO_InitTypeDef GPIO_Initure;
    
    GPIO_Initure.Pin = CT_INT_PIN;  // PB1
    GPIO_Initure.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_Initure.Pull = GPIO_NOPULL;
    GPIO_Initure.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(CT_INT_PORT, &GPIO_Initure);
}

/*
================================================================================
  2. 软件 I2C (基于 ctiic.c)
================================================================================
*/

// SDA 设为输出模式
static void SDA_Out(void)
{
    GPIO_InitTypeDef GPIO_Initure;
    GPIO_Initure.Pin = CT_SDA_PIN;
    GPIO_Initure.Mode = GPIO_MODE_OUTPUT_OD; // 开漏输出
    GPIO_Initure.Pull = GPIO_PULLUP;
    GPIO_Initure.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(CT_SDA_PORT, &GPIO_Initure);
}

// SDA 设为输入模式
static void SDA_In(void)
{
    GPIO_InitTypeDef GPIO_Initure;
    GPIO_Initure.Pin = CT_SDA_PIN;
    GPIO_Initure.Mode = GPIO_MODE_INPUT; // 输入
    GPIO_Initure.Pull = GPIO_PULLUP;     // 上拉
    HAL_GPIO_Init(CT_SDA_PORT, &GPIO_Initure);
}

// 宏定义简化
#define IIC_SCL(n)      HAL_GPIO_WritePin(CT_SCL_PORT, CT_SCL_PIN, (n ? GPIO_PIN_SET : GPIO_PIN_RESET))
#define IIC_SDA(n)      HAL_GPIO_WritePin(CT_SDA_PORT, CT_SDA_PIN, (n ? GPIO_PIN_SET : GPIO_PIN_RESET))
#define READ_SDA        HAL_GPIO_ReadPin(CT_SDA_PORT, CT_SDA_PIN)

void TOUCH_HW_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_Initure;

    __HAL_RCC_GPIOB_CLK_ENABLE();   		// 使能GPIOB时钟
    __HAL_RCC_GPIOF_CLK_ENABLE();   		// 使能GPIOF时钟

    // SCL (PB0)
    GPIO_Initure.Pin = CT_SCL_PIN;
    GPIO_Initure.Mode = GPIO_MODE_OUTPUT_OD; 
    GPIO_Initure.Pull = GPIO_PULLUP;        
    GPIO_Initure.Speed = GPIO_SPEED_HIGH;   
    HAL_GPIO_Init(CT_SCL_PORT, &GPIO_Initure);

    // SDA (PF11)
    GPIO_Initure.Pin = CT_SDA_PIN;
    HAL_GPIO_Init(CT_SDA_PORT, &GPIO_Initure);

    IIC_SDA(1);
    IIC_SCL(1);
}

void TOUCH_HW_IIC_Start(void)
{
    SDA_Out();     // sda线输出
    IIC_SDA(1);	  
    IIC_SCL(1);
    TOUCH_HW_Delay_us(2);
 	IIC_SDA(0); // START
    TOUCH_HW_Delay_us(2);
 	IIC_SCL(0); // 钳住I2C总线
    TOUCH_HW_Delay_us(2);
}	  

void TOUCH_HW_IIC_Stop(void)
{
	SDA_Out(); // sda线输出
	IIC_SCL(0);
	IIC_SDA(0); // STOP
    TOUCH_HW_Delay_us(2);
	IIC_SCL(1); 
    TOUCH_HW_Delay_us(2);
	IIC_SDA(1); // 发送I2C总线结束信号
    TOUCH_HW_Delay_us(2);
}

uint8_t TOUCH_HW_IIC_Wait_Ack(void)
{
	uint8_t ucErrTime = 0;
	SDA_In();      // SDA设置为输入  
	IIC_SDA(1); // 释放SDA
    TOUCH_HW_Delay_us(2);	   
	IIC_SCL(1);
    TOUCH_HW_Delay_us(2);	 
	while(READ_SDA)
	{
		ucErrTime++;
		if(ucErrTime > 250)
		{
			TOUCH_HW_IIC_Stop();
			return 1;
		}
        TOUCH_HW_Delay_us(1); // 稍作延时
	}
	IIC_SCL(0); // 时钟输出0 
    TOUCH_HW_Delay_us(2);
	return 0;  
} 

void TOUCH_HW_IIC_Ack(void)
{
	IIC_SCL(0);
	SDA_Out();
	IIC_SDA(0);
	TOUCH_HW_Delay_us(2);
	IIC_SCL(1);
	TOUCH_HW_Delay_us(2);
	IIC_SCL(0);
    TOUCH_HW_Delay_us(2);
}
	    
void TOUCH_HW_IIC_NAck(void)
{
	IIC_SCL(0);
	SDA_Out();
	IIC_SDA(1);
	TOUCH_HW_Delay_us(2);
	IIC_SCL(1);
	TOUCH_HW_Delay_us(2);
	IIC_SCL(0);
    TOUCH_HW_Delay_us(2);
}

void TOUCH_HW_IIC_Send_Byte(uint8_t txd)
{                        
    uint8_t t;   
	SDA_Out(); 	    
    IIC_SCL(0); // 拉低时钟开始数据传输
    for(t = 0; t < 8; t++)
    {              
        IIC_SDA((txd & 0x80) >> 7);
        TOUCH_HW_Delay_us(2);   
        IIC_SCL(1);
        TOUCH_HW_Delay_us(2); 
        IIC_SCL(0);
        TOUCH_HW_Delay_us(2);
        txd <<= 1;      
    }	 
} 	    

uint8_t TOUCH_HW_IIC_Read_Byte(unsigned char ack)
{
	unsigned char i, receive = 0;
	SDA_In(); // SDA设置为输入
    for(i = 0; i < 8; i++ )
	{
        IIC_SCL(0); 
        TOUCH_HW_Delay_us(2);
		IIC_SCL(1);
        receive <<= 1;
        if(READ_SDA) receive++;   
        TOUCH_HW_Delay_us(2);
    }					 
    if (!ack)
        TOUCH_HW_IIC_NAck();//发送nACK
    else
        TOUCH_HW_IIC_Ack(); //发送ACK   
    return receive;
}

/*
================================================================================
  3. 延时
================================================================================
*/

void TOUCH_HW_Delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/**
 * @brief 硬件：微秒延时 (需要校准)
 */
void TOUCH_HW_Delay_us(uint32_t us)
{
    // !! 注意：这是一个粗略的、未校准的延时循环。
    // 您必须根据您的 HCLK 频率和编译器优化级别 (-O0, -O2...) 对其进行校准。
    // (以下值是基于 ~168MHz HCLK 和 -O2 优化的一个粗略估计)
    volatile uint32_t ticks = us * 30; // 假设 30 个循环 ~ 1us
    if (ticks == 0) ticks = 1;
    while(ticks-- > 0);
}