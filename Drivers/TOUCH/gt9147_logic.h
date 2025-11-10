#ifndef __GT9147_LOGIC_H
#define __GT9147_LOGIC_H	
#include <stdint.h>	

/*
================================================================================
  GT9147 寄存器定义
================================================================================
*/
#define GT_CMD_WR 		0X28     	// I2C 写命令
#define GT_CMD_RD 		0X29		// I2C 读命令
  
#define GT_CTRL_REG 	0X8040   	// GT9147控制寄存器
#define GT_CFGS_REG 	0X8047   	// GT9147配置起始地址寄存器
#define GT_CHECK_REG 	0X80FF   	// GT9147校验和寄存器
#define GT_PID_REG 		0X8140   	// GT9147产品ID寄存器

#define GT_GSTID_REG 	0X814E   	// GT9147当前检测到的触摸情况
#define GT_TP1_REG 		0X8150  	// 第一个触摸点数据地址
#define GT_TP2_REG 		0X8158		// 第二个触摸点数据地址
#define GT_TP3_REG 		0X8160		// 第三个触摸点数据地址
#define GT_TP4_REG 		0X8168		// 第四个触摸点数据地址
#define GT_TP5_REG 		0X8170		// 第五个触摸点数据地址  

#define GT_MAX_POINTS   5           // 芯片支持的最大点数

#define GT9147_NEED_INT_OUTPUT_CONFIG

/*
================================================================================
  公开数据结构
================================================================================
*/

/**
 * @brief 触摸点信息结构体
 */
typedef struct 
{
	uint8_t  id;        // 触摸点的 ID (0-4)
	uint16_t x;         // 转换后的 X 坐标
	uint16_t y;         // 转换后的 Y 坐标
} GT_TouchPoint_t;


/*
================================================================================
  公开 API 函数
================================================================================
*/

/**
 * @brief 初始化GT9147触摸屏
 * @param width       当前屏幕宽度 (例如: 800)
 * @param height      当前屏幕高度 (例如: 480)
 * @param orientation 0 = 竖屏 (Portrait), 1 = 横屏 (Landscape)
 * @return 0: 初始化成功, 1: 失败 (未检测到芯片)
 */
uint8_t GT9147_Init(uint16_t width, uint16_t height, uint8_t orientation);

/**
 * @brief 扫描触摸点
 * @param points     一个数组，用于存储触摸点数据
 * @param max_points 数组的最大长度 (推荐为 GT_MAX_POINTS)
 * @return 0: 无触摸, >0: 实际检测到的触摸点数量
 */
uint8_t GT9147_Scan(GT_TouchPoint_t* points, uint8_t max_points);

#endif