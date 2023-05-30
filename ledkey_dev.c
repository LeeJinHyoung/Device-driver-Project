#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>          
#include <linux/errno.h>       
#include <linux/types.h>       
#include <linux/fcntl.h>       
#include <linux/moduleparam.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/poll.h>
#include "ioctl.h"

#define   KERTIM_DEV_NAME            "ledkeydev"
#define   KERTIM_DEV_MAJOR            240      
#define DEBUG 1
#define IMX_GPIO_NR(bank, nr)       (((bank) - 1) * 32 + (nr))

//#define TIME_STEP	ptrmng->time_val
DECLARE_WAIT_QUEUE_HEAD(WaitQueue_Read);

int irq_init(struct file *filp);

//static char * twostring = NULL;
static int sw_irq[8] = {0};
static char sw_no = 0;

typedef struct
{
	struct timer_list	timer;
	unsigned long		  led;
	int					time_val;
}__attribute__ ((packed)) KERNEL_TIMER_MANAGER;

//static KERNEL_TIMER_MANAGER* ptrmng = NULL;

void kerneltimer_timeover(unsigned long arg);

/*
typedef struct 
{
	int sw_irq[8];
	char sw_no;
}__attribute__ ((packed)) ISR_INFO;
*/
int led[4] = {
	IMX_GPIO_NR(1, 16),   //16
	IMX_GPIO_NR(1, 17),	  //17
	IMX_GPIO_NR(1, 18),   //18
	IMX_GPIO_NR(1, 19),   //19
};
int key[8] = {
	IMX_GPIO_NR(1, 20),   //20
	IMX_GPIO_NR(1, 21),	  //21
	IMX_GPIO_NR(4, 8),    //104
	IMX_GPIO_NR(4, 9),    //105
	IMX_GPIO_NR(4, 5),    //101
	IMX_GPIO_NR(7, 13),	  //205
	IMX_GPIO_NR(1, 7),    //7
	IMX_GPIO_NR(1, 8),    //8
};
static int ledkey_request(struct file* filp)
{
	int ret = 0;
	int i;
//	ISR_INFO *pIsr_Info = (ISR_INFO *)filp->private_data;
	for (i = 0; i < ARRAY_SIZE(led); i++) {
		ret = gpio_request(led[i], "gpio led");
		if(ret<0){
			printk("#### FAILED Request gpio %d. error : %d \n", led[i], ret);
			break;
		} 
  		gpio_direction_output(led[i], 0);
	}
	for (i = 0; i < ARRAY_SIZE(key); i++) {
//		sw_irq[i] = gpio_to_irq(key[i]);
		sw_irq[i] = gpio_to_irq(key[i]);
		if(ret<0){
			printk("#### FAILED Request gpio %d. error : %d \n", key[i], ret);
			break;
		} 
//  	gpio_direction_input(key[i]); 
	}
	return ret;
}
static void ledkey_free(struct file *filp)
{
	int i;
//	ISR_INFO *pIsr_Info =(ISR_INFO *)filp->private_data;
	for (i = 0; i < ARRAY_SIZE(led); i++){
		gpio_free(led[i]);
	}
  	for (i = 0; i < ARRAY_SIZE(key); i++){
//		gpio_free(key[i]);
		free_irq(sw_irq[i],filp->private_data);
	}
}

void led_write(unsigned char data)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(led); i++){
    	gpio_set_value(led[i], (data >> i ) & 0x01);
	}
#if DEBUG
	printk("#### %s, data = %d\n", __FUNCTION__, data);
#endif
}
void key_read(unsigned char * key_data)
{
	int i;
	unsigned long data=0;
	unsigned long temp;
	for(i=0;i<ARRAY_SIZE(key);i++)
	{
		temp = gpio_get_value(key[i]);
		if(temp)
		{
			data = i + 1;
			break;
		}
	}
#if DEBUG
	printk("#### %s, data = %ld\n", __FUNCTION__, data);
#endif
	*key_data = data;
	return;
}


int ledkeydev_open (struct inode *inode, struct file *filp)
{
	KERNEL_TIMER_MANAGER *ptrmng = NULL;
	//ISR_INFO * pIsrInfo;
    int num0 = MAJOR(inode->i_rdev); 
    int num1 = MINOR(inode->i_rdev); 
	int timeval = 0;

	ptrmng = (KERNEL_TIMER_MANAGER *)kmalloc( sizeof(KERNEL_TIMER_MANAGER ), GFP_KERNEL);
	
	timeval = ptrmng->time_val;	
//	pIsrInfo =(ISR_INFO *)kmalloc(sizeof(ISR_INFO),GFP_KERNEL);//100번지부터 33바이트
    printk( "ledkeydev open -> major : %d\n", num0 );
    printk( "ledkeydev open -> minor : %d\n", num1 );

//	memset(pIsrInfo,0x00, sizeof(ISR_INFO));
	

	if(ptrmng == NULL) return -ENOMEM;
	memset( ptrmng, 0, sizeof( KERNEL_TIMER_MANAGER));

	printk("timeval : %d , sec : %d , size : %d\n",timeval,timeval/HZ, sizeof(KERNEL_TIMER_MANAGER ));
	filp->private_data = (void *)ptrmng;
//	filp->private_data = (void *)pIsrInfo;
	irq_init(filp);

    return 0;
}

void kerneltimer_registertimer(KERNEL_TIMER_MANAGER *pdata, unsigned long timeover)
{
#if DEBUG
	printk("RESISTER TIMER START@@@@@@\n");
#endif
	//	int TIME_STEP = pdata->time_val;
	init_timer(&(pdata->timer));
	pdata->timer.expires = get_jiffies_64() + timeover; 
//	pdata->timer.expires = get_jiffies_64() + pdata->time_val; 
	pdata->timer.data    = (unsigned long)pdata;//timeover에 전달할 매개변수arg
	pdata->timer.function = kerneltimer_timeover;
	add_timer( &(pdata->timer) );
}

void kerneltimer_timeover(unsigned long arg) // 주소를 상수로 전달받음
{
	KERNEL_TIMER_MANAGER* pdata = NULL;
	if(arg)
	{
#if DEBUG
		printk("TIMEOVER START@@@@\n");
#endif	
		pdata = (KERNEL_TIMER_MANAGER *)arg; // 상수기 때문에 pointer로 형변환
		led_write(pdata->led&0x0f);
		pdata->led = ~(pdata->led);
//		kerneltimer_registertimer( pdata, TIME_STEP);
		kerneltimer_registertimer( pdata, pdata->time_val);
		
	}
}

ssize_t ledkeydev_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
//	char kbuf;
	int ret;
//	char * pSw_no = filp->private_data;
//	ISR_INFO *pIsrInfo = filp->private_data;
#if DEBUG
    printk( "ledkeydev read -> buf : %08X, count : %08X \n", (unsigned int)buf, count );
#endif
//	key_read(&kbuf);     
//	ret=copy_to_user(buf,&sw_no,count);
	if(!(filp->f_flags & O_NONBLOCK))
	{
		if(sw_no==0)//키가 한 번도 안눌렸다면
		{
			interruptible_sleep_on(&WaitQueue_Read);
		}
	}

	ret=copy_to_user(buf,&sw_no,count);
//  	ret=copy_to_user(buf,&pIsrInfo->sw_no,count);
//	*pSw_no = 0;
	sw_no = 0;
	if(ret < 0)
		return -ENOMEM;
    return count;
}

ssize_t ledkeydev_write (struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	KERNEL_TIMER_MANAGER* ptrmng = (KERNEL_TIMER_MANAGER *)filp->private_data;
	char kbuf;
	int ret;
#if DEBUG
    printk( "ledkeydev write -> buf : %08X, count : %08X \n", (unsigned int)buf, count );
#endif
	ret=copy_from_user(&kbuf,buf,count);
	if(ret < 0)
		return -ENOMEM;
//	led_write(kbuf);
	ptrmng->led = kbuf;
    return count;
}

static long ledkeydev_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	keyled_data ctrl_info = {0};
	KERNEL_TIMER_MANAGER *ptrmng = (KERNEL_TIMER_MANAGER *)filp->private_data;
	
//	int TIME_STEP = filp->private_data.time_val;
	int err, size;
	if( _IOC_TYPE( cmd ) != IOCTLTEST_MAGIC ) return -EINVAL;
	if( _IOC_NR( cmd ) >= IOCTLTEST_MAXNR ) return -EINVAL;

	size = _IOC_SIZE( cmd );//구조체 크기 return .. 지금은 4
	if(size)
	{
		err = 0;
		if( _IOC_DIR( cmd ) & _IOC_READ )
			//arg는 구조체 주소.. 현재 정수로 들어왔기 때문에 형변환
			err = access_ok( VERIFY_WRITE, (void *) arg, size );
		if( _IOC_DIR( cmd ) & _IOC_WRITE )//읽고 쓸 수 있는지 확인
			err = access_ok( VERIFY_READ , (void *) arg, size );
		if( !err ) return err;
	}
	switch(cmd)
	{
		//char buf;
		case TIMER_START:
			if(!timer_pending(&(ptrmng->timer)))
			{
#if DEBUG
				printk("timer start\n");
#endif
				kerneltimer_registertimer( ptrmng, ptrmng->time_val);
			}
			break;
		case TIMER_VALUE:
			err=copy_from_user((void *)&ctrl_info,(void *)arg,sizeof(ctrl_info));
			ptrmng->time_val=ctrl_info.timer_val;
#if DEBUG
			printk("timer value: %d\n", ptrmng->time_val);
#endif		
			break;
		case TIMER_STOP:
			if(timer_pending(&(ptrmng->timer))) // 타이머가 등록되어있다면..
			{	
				del_timer(&(ptrmng->timer));
#if DEBUG
				printk("timer stop\n");
#endif		
			}
			break;
		default:
			err = -E2BIG;
			break;
	}
	return err;
}

static unsigned int ledkeydev_poll(struct file *filp, struct poll_table_struct *wait)
{
//	ISR_INFO *pIsrInfo =(ISR_INFO *)filp->private_data;
//	char* pSw_no = &pIsrInfo->sw_no;
	unsigned int mask = 0;
//	printk("_key : %ld \n", (wait->_key & POLLIN));
	if(wait->_key&POLLIN)
		poll_wait(filp,&WaitQueue_Read,wait);
	if(sw_no>0)
	{
		mask = POLLIN;
	}
	return mask;
}
int ledkeydev_release (struct inode *inode, struct file *filp)
{
	KERNEL_TIMER_MANAGER *ptrmng = (KERNEL_TIMER_MANAGER *)filp->private_data;
    printk( "ledkeydev release \n" );
	ledkey_free(filp);

	if(timer_pending(&(ptrmng->timer)))
		del_timer(&(ptrmng->timer));
		

	if(ptrmng != NULL)
	{
		kfree(ptrmng);
	}
    return 0;
}

struct file_operations ledkeydev_fops =
{
    .owner    = THIS_MODULE,
    .open     = ledkeydev_open,     
    .read     = ledkeydev_read,     
    .write    = ledkeydev_write,    
	.unlocked_ioctl = ledkeydev_ioctl,
    .poll	  = ledkeydev_poll,
	.release  = ledkeydev_release,  
};

irqreturn_t sw_isr(int irq, void *private_data)
{
	int i;
//	ISR_INFO *pIsrInfo = (ISR_INFO *)private_data;
//	char *pSw_no = &pIsrInfo->sw_no;
	for(i=0;i<ARRAY_SIZE(key);i++)
	{
		if(irq == sw_irq[i])
		{
			sw_no = i+1;
//			pIsrInfo->sw_no = i+1;
			break;
		}
	}
#if DEBUG
	printk("IRQ : %d, sw_no : %d\n",irq,sw_no);
#endif
	wake_up_interruptible(&WaitQueue_Read);
	return IRQ_HANDLED;
}

int irq_init(struct file *filp)
{
	int i;
	char * sw_name[8] = {"key1","key2","key3","key4","key5","key6","key7","key8"};
//	int result = ledkey_request();
	int result = ledkey_request(filp);
	
//	ISR_INFO *pSw_irq =(ISR_INFO *)filp->private_data;
	if(result < 0)		
	{
  		return result;     /* Device or resource busy */
	}
	for(i=0;i<ARRAY_SIZE(key);i++)
	{
//		result = request_irq(sw_irq[i],sw_isr,IRQF_TRIGGER_RISING,sw_name[i],filp->private_data);
		result = request_irq(sw_irq[i],sw_isr,IRQF_TRIGGER_RISING,sw_name[i],filp->private_data);//8키 모두 다 같은 irq핸들러인 sw_isr을 사용중.
		if(result)
		{
			printk("#### FAILED Request irq %d. error : %d \n", sw_irq[i], result);
			break;
		}
	}
	return result;
}

int ledkeydev_init(void)
{
    int result=0;
#if DEBUG
    printk( "ledkeydev ledkeydev_init \n" );    
#endif
    result = register_chrdev( KERTIM_DEV_MAJOR, KERTIM_DEV_NAME, &ledkeydev_fops);
    if (result < 0) return result;

    return result;
}

void ledkeydev_exit(void)
{
    printk( "ledkeydev ledkeydev_exit \n" );    
    unregister_chrdev( KERTIM_DEV_MAJOR, KERTIM_DEV_NAME );
}

module_init(ledkeydev_init);
module_exit(ledkeydev_exit);

MODULE_AUTHOR("LJH");
MODULE_DESCRIPTION("test module");
MODULE_LICENSE("Dual BSD/GPL");
