/*
 * USB CDC ACM with CherryRingBuffer Integration
 */

#include "cdc_acm_ringbuffer.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "chry_ringbuffer.h"  // 引入CherryRingBuffer头文件

/*!< endpoint address */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x83

#define USBD_VID           0xFFFF
#define USBD_PID           0xFFFF
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

#ifdef CONFIG_USB_HS
#define CDC_MAX_MPS 512
#else
#define CDC_MAX_MPS 64
#endif

/* ========== RingBuffer配置 ========== */
/* 注意：size必须是2的幂次方！如：512, 1024, 2048, 4096, 8192 */
#define CDC_RX_RINGBUF_SIZE  (4096)  // 接收环形缓冲区大小
#define CDC_TX_RINGBUF_SIZE  (4096)  // 发送环形缓冲区大小
#define CDC_USB_READ_SIZE    (2048)  // USB单次读取大小

/* RingBuffer实例 */
static chry_ringbuffer_t rx_ringbuf;
static chry_ringbuffer_t tx_ringbuf;

/* RingBuffer存储空间 */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t rx_ringbuf_pool[CDC_RX_RINGBUF_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t tx_ringbuf_pool[CDC_TX_RINGBUF_SIZE];

/* USB临时缓冲区 */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usb_read_buffer[CDC_USB_READ_SIZE];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t usb_write_buffer[CDC_USB_READ_SIZE];

volatile bool ep_tx_busy_flag = false;
volatile uint8_t dtr_enable = 0;

/* ========== 函数前向声明 ========== */
static void usbd_event_handler(uint8_t busid, uint8_t event);
void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes);
void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes);
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr);
void cdc_acm_try_send(uint8_t busid);

/* ========== 描述符定义 (保持原样) ========== */
#ifdef CONFIG_USBDEV_ADVANCE_DESC
static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02)
};

static const uint8_t device_quality_descriptor[] = {
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },
    "PolarisYu",
    "Polaris CDC DEMO",
    "2052125840",
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    return config_descriptor;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    return device_quality_descriptor;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    if (index > 3) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor cdc_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};
#else
/*!< global descriptor */
static const uint8_t cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, CDC_MAX_MPS, 0x02),
    USB_LANGID_INIT(USBD_LANGID_STRING),
    0x14, USB_DESCRIPTOR_TYPE_STRING,
    'C', 0x00, 'h', 0x00, 'e', 0x00, 'r', 0x00, 'r', 0x00, 'y', 0x00,
    'U', 0x00, 'S', 0x00, 'B', 0x00,
    0x26, USB_DESCRIPTOR_TYPE_STRING,
    'C', 0x00, 'h', 0x00, 'e', 0x00, 'r', 0x00, 'r', 0x00, 'y', 0x00,
    'U', 0x00, 'S', 0x00, 'B', 0x00, ' ', 0x00, 'C', 0x00, 'D', 0x00,
    'C', 0x00, ' ', 0x00, 'D', 0x00, 'E', 0x00, 'M', 0x00, 'O', 0x00,
    0x16, USB_DESCRIPTOR_TYPE_STRING,
    '2', 0x00, '0', 0x00, '2', 0x00, '4', 0x00, '0', 0x00,
    '1', 0x00, '0', 0x00, '1', 0x00, '0', 0x00, '0', 0x00,
#ifdef CONFIG_USB_HS
    0x0a, USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
#endif
    0x00
};
#endif

/* ========== RingBuffer初始化函数 ========== */
static int cdc_ringbuffer_init(void)
{
    int ret;
    
    // 初始化接收环形缓冲区
    ret = chry_ringbuffer_init(&rx_ringbuf, rx_ringbuf_pool, CDC_RX_RINGBUF_SIZE);
    if (ret != 0) {
        USB_LOG_ERR("RX ringbuffer init failed\r\n");
        return ret;
    }
    
    // 初始化发送环形缓冲区
    ret = chry_ringbuffer_init(&tx_ringbuf, tx_ringbuf_pool, CDC_TX_RINGBUF_SIZE);
    if (ret != 0) {
        USB_LOG_ERR("TX ringbuffer init failed\r\n");
        return ret;
    }
    
    USB_LOG_INFO("CDC RingBuffer initialized (RX:%d, TX:%d)\r\n", 
                 CDC_RX_RINGBUF_SIZE, CDC_TX_RINGBUF_SIZE);
    return 0;
}

/* ========== USB事件处理 ========== */
static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
        case USBD_EVENT_RESET:
            // 复位时清空环形缓冲区
            chry_ringbuffer_reset(&rx_ringbuf);
            chry_ringbuffer_reset(&tx_ringbuf);
            break;
            
        case USBD_EVENT_CONNECTED:
            break;
            
        case USBD_EVENT_DISCONNECTED:
            // 断开连接时清空缓冲区
            chry_ringbuffer_reset(&rx_ringbuf);
            chry_ringbuffer_reset(&tx_ringbuf);
            ep_tx_busy_flag = false;
            break;
            
        case USBD_EVENT_RESUME:
            break;
            
        case USBD_EVENT_SUSPEND:
            break;
            
        case USBD_EVENT_CONFIGURED:
            ep_tx_busy_flag = false;
            // 启动第一次USB接收
            usbd_ep_start_read(busid, CDC_OUT_EP, usb_read_buffer, CDC_USB_READ_SIZE);
            break;
            
        case USBD_EVENT_SET_REMOTE_WAKEUP:
            break;
            
        case USBD_EVENT_CLR_REMOTE_WAKEUP:
            break;

        default:
            break;
    }
}

/* ========== USB批量输出回调 (主机->设备) ========== */
void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if (nbytes > 0) {
        // 将接收到的数据写入接收环形缓冲区
        uint32_t written = chry_ringbuffer_write(&rx_ringbuf, usb_read_buffer, nbytes);
        
        if (written < nbytes) {
            // 缓冲区满，数据丢失
            USB_LOG_WRN("RX buffer overflow, lost %ld bytes\r\n", nbytes - written);
        }
        
        USB_LOG_DBG("Received %d bytes, buffered %ld bytes\r\n", nbytes, written);
    }
    
    // 继续启动下一次USB接收
    usbd_ep_start_read(busid, CDC_OUT_EP, usb_read_buffer, CDC_USB_READ_SIZE);
}

/* ========== USB批量输入回调 (设备->主机) ========== */
void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    USB_LOG_DBG("Sent %d bytes\r\n", nbytes);

    // 处理ZLP (Zero Length Packet)
    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
        usbd_ep_start_write(busid, CDC_IN_EP, NULL, 0);
    } else {
        ep_tx_busy_flag = false;
        
        // 发送完成后，检查是否还有待发送数据
        cdc_acm_try_send(busid);
    }
}

/* ========== 端点定义 ========== */
struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out
};

struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in
};

static struct usbd_interface intf0;
static struct usbd_interface intf1;

/* ========== DTR控制 ========== */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    if (dtr) {
        dtr_enable = 1;
        // DTR使能时尝试发送缓冲区中的数据
        cdc_acm_try_send(busid);
    } else {
        dtr_enable = 0;
    }
}

/* ========== 初始化函数 ========== */
void cdc_acm_init(uint8_t busid, uintptr_t reg_base)
{
    // 初始化环形缓冲区
    if (cdc_ringbuffer_init() != 0) {
        USB_LOG_ERR("CDC RingBuffer init failed!\r\n");
        return;
    }
    
#ifdef CONFIG_USBDEV_ADVANCE_DESC
    usbd_desc_register(busid, &cdc_descriptor);
#else
    usbd_desc_register(busid, cdc_descriptor);
#endif

    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);
    usbd_initialize(busid, reg_base, usbd_event_handler);
}

/* ========== 应用层API：尝试发送数据 ========== */
void cdc_acm_try_send(uint8_t busid)
{
    // 检查是否可以发送
    if (!dtr_enable || ep_tx_busy_flag) {
        return;
    }
    
    // 从发送环形缓冲区读取数据
    uint32_t available = chry_ringbuffer_get_used(&tx_ringbuf);
    if (available == 0) {
        return;  // 没有待发送数据
    }
    
    // 限制单次发送大小
    uint32_t send_size = (available > CDC_USB_READ_SIZE) ? CDC_USB_READ_SIZE : available;
    
    // 从环形缓冲区读取数据到USB发送缓冲区
    uint32_t read_size = chry_ringbuffer_read(&tx_ringbuf, usb_write_buffer, send_size);
    
    if (read_size > 0) {
        ep_tx_busy_flag = true;
        usbd_ep_start_write(busid, CDC_IN_EP, usb_write_buffer, read_size);
    }
}

/* ========== 应用层API：写入数据到发送缓冲区 ========== */
int cdc_acm_send_data(uint8_t busid, const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return -1;
    }
    
    // 写入发送环形缓冲区
    uint32_t written = chry_ringbuffer_write(&tx_ringbuf, (void *)data, len);
    
    // 立即尝试发送
    cdc_acm_try_send(busid);
    
    USB_LOG_INFO("Written %ld bytes\r\n", written);
    return (int)written;
}

/* ========== 应用层API：从接收缓冲区读取数据 ========== */
int cdc_acm_read_data(uint8_t *buffer, uint32_t max_len)
{
    if (buffer == NULL || max_len == 0) {
        return 0;
    }

    uint32_t used = chry_ringbuffer_get_used(&rx_ringbuf);
    USB_LOG_INFO("Read %ld bytes\r\n", used);

    return (int)chry_ringbuffer_read(&rx_ringbuf, buffer, max_len);
}

/* ========== 应用层API：查看接收数据但不移除 ========== */
int cdc_acm_peek_data(uint8_t *buffer, uint32_t max_len)
{
    if (buffer == NULL || max_len == 0) {
        return 0;
    }
    
    return (int)chry_ringbuffer_peek(&rx_ringbuf, buffer, max_len);
}

/* ========== 应用层API：获取接收缓冲区可用数据量 ========== */
uint32_t cdc_acm_get_rx_available(void)
{
    return chry_ringbuffer_get_used(&rx_ringbuf);
}

/* ========== 应用层API：获取发送缓冲区剩余空间 ========== */
uint32_t cdc_acm_get_tx_free(void)
{
    return chry_ringbuffer_get_free(&tx_ringbuf);
}

/* ========== 应用层API：检查接收缓冲区是否为空 ========== */
bool cdc_acm_is_rx_empty(void)
{
    return chry_ringbuffer_check_empty(&rx_ringbuf);
}

/* ========== 应用层API：检查发送缓冲区是否满 ========== */
bool cdc_acm_is_tx_full(void)
{
    return chry_ringbuffer_check_full(&tx_ringbuf);
}

/* ========== 应用层API：清空接收缓冲区 ========== */
void cdc_acm_flush_rx(void)
{
    chry_ringbuffer_reset(&rx_ringbuf);
}

/* ========== 应用层API：清空发送缓冲区 ========== */
void cdc_acm_flush_tx(void)
{
    chry_ringbuffer_reset(&tx_ringbuf);
}

/* ========== 应用层API：丢弃指定字节的接收数据 ========== */
uint32_t cdc_acm_drop_rx(uint32_t size)
{
    return chry_ringbuffer_drop(&rx_ringbuf, size);
}

/* ========== 高级API：使用线性缓冲区进行零拷贝读取（适合DMA） ========== */
void *cdc_acm_linear_read_setup(uint32_t *size)
{
    return chry_ringbuffer_linear_read_setup(&rx_ringbuf, size);
}

void cdc_acm_linear_read_done(uint32_t size)
{
    chry_ringbuffer_linear_read_done(&rx_ringbuf, size);
}

/* ========== 高级API：使用线性缓冲区进行零拷贝写入（适合DMA） ========== */
void *cdc_acm_linear_write_setup(uint32_t *size)
{
    return chry_ringbuffer_linear_write_setup(&tx_ringbuf, size);
}

void cdc_acm_linear_write_done(uint8_t busid, uint32_t size)
{
    chry_ringbuffer_linear_write_done(&tx_ringbuf, size);
    cdc_acm_try_send(busid);
}


/* ========== 使用示例 ========== */
#if 0
/* 示例1：基本收发 */
void example_basic_echo(uint8_t busid)
{
    uint8_t buffer[256];
    
    // 检查并读取接收数据
    if (!cdc_acm_is_rx_empty()) {
        int len = cdc_acm_read_data(buffer, sizeof(buffer));
        if (len > 0) {
            // 回显数据
            cdc_acm_send_data(busid, buffer, len);
        }
    }
    
    // 确保数据发送
    cdc_acm_try_send(busid);
}

/* 示例2：按行处理 */
void example_line_processing(uint8_t busid)
{
    static uint8_t line_buffer[256];
    static uint32_t line_pos = 0;
    uint8_t byte;
    
    while (chry_ringbuffer_read_byte(&rx_ringbuf, &byte)) {
        if (byte == '\n' || byte == '\r') {
            if (line_pos > 0) {
                // 处理完整的一行
                process_command(line_buffer, line_pos);
                line_pos = 0;
            }
        } else if (line_pos < sizeof(line_buffer) - 1) {
            line_buffer[line_pos++] = byte;
        }
    }
}

/* 示例3：DMA零拷贝读取 */
void example_dma_read(void)
{
    uint32_t size;
    uint8_t *ptr = cdc_acm_linear_read_setup(&size);
    
    if (size > 0) {
        // 启动DMA传输
        dma_start_transfer(DMA_CH0, ptr, dst_addr, size);
        // DMA完成后调用
        cdc_acm_linear_read_done(size);
    }
}

/* 示例4：DMA零拷贝写入 */
void example_dma_write(uint8_t busid)
{
    uint32_t size;
    uint8_t *ptr = cdc_acm_linear_write_setup(&size);
    
    if (size > 0) {
        // 启动DMA传输
        dma_start_transfer(DMA_CH1, src_addr, ptr, size);
        // DMA完成后调用
        cdc_acm_linear_write_done(busid, size);
    }
}

/* 示例5：主循环集成 */
int main(void)
{
    uint8_t busid = 0;
    
    system_init();
    cdc_acm_init(busid, USB_BASE);
    
    while (1) {
        // 处理接收数据
        example_basic_echo(busid);
        
        // 或者按行处理
        // example_line_processing(busid);
        
        // 定期发送心跳
        static uint32_t tick = 0;
        if (++tick % 10000 == 0) {
            const char *msg = "Heartbeat\r\n";
            cdc_acm_send_data(busid, (uint8_t *)msg, strlen(msg));
        }
        
        // 其他任务
        other_tasks();
    }
}
#endif