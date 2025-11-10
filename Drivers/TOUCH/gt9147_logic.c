#include "gt9147_logic.h"
#include "touch_hal_port.h"
#include <string.h>

// ============================================================================
// 静态变量
// ============================================================================
static uint16_t s_lcd_width = 0;
static uint16_t s_lcd_height = 0;
static uint8_t s_lcd_orientation = 0;

// 用于保存上一次有效的触摸坐标（用于非法数据恢复）
static uint16_t s_last_valid_x = 0;
static uint16_t s_last_valid_y = 0;

// CPU 占用率优化计数器
static uint8_t s_scan_counter = 0;

// ============================================================================
// 常量定义
// ============================================================================
const uint16_t GT9147_TPX_TBL[GT_MAX_POINTS] = {
    GT_TP1_REG, GT_TP2_REG, GT_TP3_REG, GT_TP4_REG, GT_TP5_REG
};

const uint8_t GT9147_CFG_TBL[] = { 
	0X60,0XE0,0X01,0X20,0X03,0X05,0X35,0X00,0X02,0X08,
	0X1E,0X08,0X50,0X3C,0X0F,0X05,0X00,0X00,0XFF,0X67,
	0X50,0X00,0X00,0X18,0X1A,0X1E,0X14,0X89,0X28,0X0A,
	0X30,0X2E,0XBB,0X0A,0X03,0X00,0X00,0X02,0X33,0X1D,
	0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X32,0X00,0X00,
	0X2A,0X1C,0X5A,0X94,0XC5,0X02,0X07,0X00,0X00,0X00,
	0XB5,0X1F,0X00,0X90,0X28,0X00,0X77,0X32,0X00,0X62,
	0X3F,0X00,0X52,0X50,0X00,0X52,0X00,0X00,0X00,0X00,
	0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,
	0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X0F,
	0X0F,0X03,0X06,0X10,0X42,0XF8,0X0F,0X14,0X00,0X00,
	0X00,0X00,0X1A,0X18,0X16,0X14,0X12,0X10,0X0E,0X0C,
	0X0A,0X08,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,
	0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,
	0X00,0X00,0X29,0X28,0X24,0X22,0X20,0X1F,0X1E,0X1D,
	0X0E,0X0C,0X0A,0X08,0X06,0X05,0X04,0X02,0X00,0XFF,
	0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,
	0X00,0XFF,0XFF,0XFF,0XFF,0XFF,0XFF,0XFF,0XFF,0XFF,
	0XFF,0XFF,0XFF,0XFF,
};

// ============================================================================
// 底层 I2C 封装
// ============================================================================

uint8_t GT9147_WR_Reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
	uint8_t i;
	uint8_t ret = 0;
	
	TOUCH_HW_IIC_Start();	
 	TOUCH_HW_IIC_Send_Byte(GT_CMD_WR);
	if(TOUCH_HW_IIC_Wait_Ack()) { ret = 1; goto stop; }
	
	TOUCH_HW_IIC_Send_Byte(reg >> 8);
	if(TOUCH_HW_IIC_Wait_Ack()) { ret = 1; goto stop; }
	
	TOUCH_HW_IIC_Send_Byte(reg & 0XFF);
	if(TOUCH_HW_IIC_Wait_Ack()) { ret = 1; goto stop; }
	
	for(i = 0; i < len; i++)
	{
    	TOUCH_HW_IIC_Send_Byte(buf[i]);
		ret = TOUCH_HW_IIC_Wait_Ack();
		if(ret) break;
	}
	
stop:
    TOUCH_HW_IIC_Stop();
	return ret; 
}

void GT9147_RD_Reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
	uint8_t i; 
	
 	TOUCH_HW_IIC_Start();	
 	TOUCH_HW_IIC_Send_Byte(GT_CMD_WR);
	TOUCH_HW_IIC_Wait_Ack();
	
 	TOUCH_HW_IIC_Send_Byte(reg >> 8);
	TOUCH_HW_IIC_Wait_Ack();
	
 	TOUCH_HW_IIC_Send_Byte(reg & 0XFF);
	TOUCH_HW_IIC_Wait_Ack();
	
 	TOUCH_HW_IIC_Start();
	TOUCH_HW_IIC_Send_Byte(GT_CMD_RD);
	TOUCH_HW_IIC_Wait_Ack();
	
	for(i = 0; i < len; i++)
	{
    	buf[i] = TOUCH_HW_IIC_Read_Byte(i == (len - 1) ? 0 : 1);
	}
	
    TOUCH_HW_IIC_Stop();
}

uint8_t GT9147_Send_Cfg(uint8_t mode)
{
	uint8_t buf[2];
	uint8_t i = 0;
	
	buf[0] = 0;
	buf[1] = mode;
	
	for(i = 0; i < sizeof(GT9147_CFG_TBL); i++) {
		buf[0] += GT9147_CFG_TBL[i];
	}
	buf[0] = (~buf[0]) + 1;
	
	GT9147_WR_Reg(GT_CFGS_REG, (uint8_t*)GT9147_CFG_TBL, sizeof(GT9147_CFG_TBL));
	GT9147_WR_Reg(GT_CHECK_REG, buf, 2);
	
	return 0;
}

// ============================================================================
// 公开 API 函数
// ============================================================================

uint8_t GT9147_Init(uint16_t width, uint16_t height, uint8_t orientation)
{
	uint8_t temp[5]; 
    
    s_lcd_width = width;
    s_lcd_height = height;
    s_lcd_orientation = orientation;
    s_last_valid_x = 0;
    s_last_valid_y = 0;
    s_scan_counter = 0;

    TOUCH_HW_GPIO_Init();
	TOUCH_HW_IIC_Init();
    
    // 硬件复位
	TOUCH_HW_Set_RST(0);
	TOUCH_HW_Delay_ms(10);
 	TOUCH_HW_Set_RST(1);
	TOUCH_HW_Delay_ms(10);
	
	// ⭐ 关键修复：INT 引脚需要临时配置为输出
	// 这是为了配置 GT9147 的 I2C 地址（参考原始代码 113-122 行）
	#ifdef GT9147_NEED_INT_OUTPUT_CONFIG
	{
		// 您需要在 touch_hal_port.c 中实现这个函数
		// 将 PB1 临时配置为输出模式
		TOUCH_HW_Set_INT_Mode_Output();
	}
	#endif
	
	TOUCH_HW_Delay_ms(100);
	
	// 读取产品ID
	GT9147_RD_Reg(GT_PID_REG, temp, 4);
	temp[4] = 0;
	
	if(strcmp((char*)temp, "9147") == 0)
	{
		temp[0] = 0X02;
		GT9147_WR_Reg(GT_CTRL_REG, temp, 1);
		
		GT9147_RD_Reg(GT_CFGS_REG, temp, 1);
		if(temp[0] < 0X60)
		{
			GT9147_Send_Cfg(1);
		}
		
		TOUCH_HW_Delay_ms(10);
		temp[0] = 0X00;
		GT9147_WR_Reg(GT_CTRL_REG, temp, 1);
		
		return 0;
	}
	
	return 1;
}

uint8_t GT9147_Scan(GT_TouchPoint_t* points, uint8_t max_points)
{
    uint8_t buf[4];
    uint8_t i = 0;
    uint8_t point_status = 0;
    uint8_t point_count = 0;
    uint8_t clear_status = 0;
    uint8_t valid_point_count = 0;
    
    // 1. 参数校验
    if(points == NULL || max_points == 0) {
        return 0;
    }
    
    // 2. 读取状态寄存器
	GT9147_RD_Reg(GT_GSTID_REG, &point_status, 1);
    
    // 3. 检查 Buffer Status (bit 7)
    //    如果 Buffer 未准备好 (bit 7 == 0)，则没有新事件。
    //    我们必须返回 0，这将被应用层状态机正确处理为 IDLE 或 HOLDING（无状态变化）。
	if ((point_status & 0x80) == 0)
	{
		return 0; // 没有新数据
	}
    
    // --- Buffer 准备好了 (bit 7 is 1) ---

    // 4. 获取触摸点数量 (bits 3-0)
    point_count = point_status & 0x0F;
    
    // 5. [关键修复]
    //    无论有多少个点（>0 或 ==0），我们都必须清除状态寄存器。
    //    这告诉芯片我们已经处理了这一帧，并准备好接收下一帧。
    GT9147_WR_Reg(GT_GSTID_REG, &clear_status, 1);

    // 6. 检查触摸点
    if (point_count == 0 || point_count > GT_MAX_POINTS)
    {
        return 0; // 没有有效点（可能是“抬起”事件，返回 0）
    }

    // 限制最大点数
    if (point_count > max_points) {
        point_count = max_points;
    }

    // 7. 循环读取坐标
	for(i = 0; i < point_count; i++)
	{
		GT9147_RD_Reg(GT9147_TPX_TBL[i], buf, 4);
		
		uint16_t raw_x = ((uint16_t)buf[1] << 8) + buf[0];
		uint16_t raw_y = ((uint16_t)buf[3] << 8) + buf[2];
		
		uint16_t temp_x, temp_y;
		
        // 8. 坐标转换
		if(s_lcd_orientation == 1) // 横屏
		{
			temp_y = raw_x;
			temp_x = s_lcd_width - raw_y;
		}
		else // 竖屏
		{
			temp_x = raw_x;
			temp_y = raw_y;
		}
		
        // 9. 坐标校验（过滤掉屏幕外的非法点）
		if(temp_x < s_lcd_width && temp_y < s_lcd_height)
		{
			points[valid_point_count].x = temp_x;
			points[valid_point_count].y = temp_y;
			points[valid_point_count].id = i;
			valid_point_count++;
		}
	}
    
	return valid_point_count;
}