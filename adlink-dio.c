/* Copyright (C) 2018 AdlinkTech, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/sysfs.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

#define DIO_SET_MAX	4  

#define OUTPUT_H	1
#define OUTPUT_L	0
#define SET_OUTPUT(val,reverse)		((reverse==0)?val:((~val)&0x1))

/* M100 : input is High-Z if not connect.
 *
 *
 *
 */
#define INPUT_H 1
#define INPUT_L 0
#define	GET_INPUT(val,reverse)		((reverse==0)?val:((~val)&0x1))

struct adlink_dio_data{
	struct mutex DIOmutex;
	struct platform_device *pdev;
	struct class *adlink_dio_class;
	char prjName[32];
	unsigned int diset;
	unsigned int doset;
	unsigned int oReverse;
	unsigned int iReverse;
	int gpio_di[DIO_SET_MAX];
	int gpio_do[DIO_SET_MAX];
};

struct platform_device *pdev_dio;


static ssize_t DI_show(struct class *c,
                                struct class_attribute *attr,
                                char *data)
{
	int gpio_di_index;
	int DIval = 0;
	int ret;
	const char *ptr;

	struct adlink_dio_data *dio_data = platform_get_drvdata(pdev_dio);

	ptr = attr->attr.name + strlen("DI_");
	ret = kstrtoint(ptr,10,&gpio_di_index);
	gpio_di_index -= 1;
	
	mutex_lock(&dio_data->DIOmutex);
	DIval = gpio_get_value(dio_data->gpio_di[gpio_di_index]);
	//printk("DO_GET : [%d]%d = %d,%d\n",gpio_di_index,dio_data->gpio_di[gpio_di_index],DIval,GET_INPUT(DIval,dio_data->iReverse));
	mutex_unlock(&dio_data->DIOmutex);

	return sprintf(data,"%d",GET_INPUT(DIval,dio_data->iReverse));
}

static ssize_t DO_set(struct class *cls,
                                struct class_attribute *attr,
                                const char *buf, size_t count)
{
	int ret;
	unsigned int udata;
	struct adlink_dio_data *dio_data = platform_get_drvdata(pdev_dio);
	int gpio_do_index;
	const char *ptr;

	/* get user data */
	ret = kstrtoint(buf,10,&udata);
	if((ret != 0)||(udata!=0 && udata!=1)){
		printk("Invalid value, only 0/1 support\n");
		return -EINVAL;
	}

	/* get DO index */
	ptr = attr->attr.name + strlen("DO_");
	ret = kstrtoint(ptr,10,&gpio_do_index);
	gpio_do_index -= 1;

	mutex_lock(&dio_data->DIOmutex);
	//printk("DO_SET : [%d]%d = %d\n",gpio_do_index,dio_data->gpio_do[gpio_do_index],SET_OUTPUT(udata,dio_data->oReverse));
	gpio_set_value(dio_data->gpio_do[gpio_do_index],SET_OUTPUT(udata,dio_data->oReverse));
	mutex_unlock(&dio_data->DIOmutex);
	
	return (ret==0)?count:-EINVAL;
}


static struct class_attribute adlink_dio_attr_4[] = {	
	
	/* 1. DI 1 */
	__ATTR(DI_1, 0440 ,DI_show,NULL),	

	/* 2.  DI 2 */
	__ATTR(DI_2,0440,DI_show,NULL),
	
	/* 3. DI 3 */
	__ATTR(DI_3,0440,DI_show,NULL),

	/* 4. DI 4 */
	__ATTR(DI_4,0440,DI_show,NULL),
	
	 
	/* 5. DO 1 */
	__ATTR(DO_1, 0220 , NULL, DO_set),

	/* 6. DO 2 */
	__ATTR(DO_2, 0220 , NULL, DO_set),

	/* 7. DO 3 */
	__ATTR(DO_3, 0220 , NULL, DO_set),

	/* 8. DO 4 */
	__ATTR(DO_4, 0220 , NULL, DO_set),

	__ATTR_NULL,
};

void adlink_dio_attrs_release(struct class *cls)
{
	kfree(cls);
}

struct class adlink_dio_class_DI_DO = {
        .name =         "adlink_DIO",
        .owner =        THIS_MODULE,
        .class_release = adlink_dio_attrs_release,
        //.class_attrs =  adlink_dio_attr_4,
};


static const struct of_device_id adlink_dio_of_match[] = {
	{ .compatible = "adlink,adlink-dio", },
	{ },
};
MODULE_DEVICE_TABLE(of, adlink_dio_of_match);

/*	Device Tree : 
 *	
 *	(1)compatible    : string ,"adlink,adlink-dio"
 *	(2)prjName       : string , specify project name. 
 *	(3)disets        : unsigned int, specify how many set of DI.
 *	(4)dosets        : unsigned int, specify how many set of DO.
 *	(5)outputReverse : unsigned int, base on hardware design.
 *	                   0: do not reverse(CPU H = output H) 
 *	                   1: reverse DO    (CPU H = output L)
 *	(6)inputReverse  : unsigned int, base on hardware design.
 *	                   0: do not reverse(Input H = CPU H)
 *	                   1: reverse DI    (Input H = CPU L)
 *
 */

static int adlink_DIO_probe(struct platform_device *pdev)
{
	struct adlink_dio_data *dio_data;
	int ret,i;
	char tmp[16];
			
	dio_data = devm_kmalloc(&pdev->dev,sizeof(struct adlink_dio_data),GFP_KERNEL);
	if (dio_data==NULL) {
		dev_err(&pdev->dev, "null of adlink dio data\n");
		return -ENOMEM;
	}else{
		memset(dio_data,0,sizeof(struct adlink_dio_data));
	}

	/* Get how many DI */
	ret = of_property_read_u32(pdev->dev.of_node,"disets",&dio_data->diset);
	if(ret<0){
		devm_kfree(&pdev->dev,dio_data);
		printk("Request DI number error\n");
		return -ENOENT;
	}
	/* Get how many DO */
	ret = of_property_read_u32(pdev->dev.of_node,"dosets",&dio_data->doset);
	if(ret<0){
		devm_kfree(&pdev->dev,dio_data);
		printk("Request DO number error\n");
		return -ENOENT;
	}

	/* Get DI reverse status*/
	ret = of_property_read_u32(pdev->dev.of_node,"inputReverse",&dio_data->iReverse);
	if(ret<0){
		devm_kfree(&pdev->dev,dio_data);
		printk("Request DI reverse status error\n");
		return -ENOENT;
	}

	/* Get DO reverse status*/
	ret = of_property_read_u32(pdev->dev.of_node,"outputReverse",&dio_data->oReverse);
	if(ret<0){
		devm_kfree(&pdev->dev,dio_data);
		printk("Request DO reverse status error\n");
		return -ENOENT;
	}

	/* Get DI gpio */
	for(i=0,ret=0;i<dio_data->diset;i++){
		sprintf(tmp,"DI_%d",i);
		dio_data->gpio_di[i] = of_get_named_gpio(pdev->dev.of_node,"DI",i);
		if(!gpio_is_valid(dio_data->gpio_di[i])){
			printk("GET Adlink DI%d : ERROR\n",i+1);
			ret = -1;
			break;
		}else{
			ret = gpio_request(dio_data->gpio_di[i],tmp);
			if(ret != 0){
				printk("[%s]GPIO request error : %s\n",__FUNCTION__,tmp);
				break;
			}
			gpio_direction_input(dio_data->gpio_di[i]);
		}				
	}
	if(ret != 0){
		devm_kfree(&pdev->dev,dio_data);
		printk("Request GPIO input error\n");
		return -EIO;
	}
	/* Get DO gpio */
	for(i=0,ret=0;i<dio_data->doset;i++){
		sprintf(tmp,"DO_%d",i);
		dio_data->gpio_do[i] = of_get_named_gpio(pdev->dev.of_node,"DO",i);
		if(!gpio_is_valid(dio_data->gpio_do[i])){
			printk("GET Adlink DO%d : ERROR\n",i+1);
			ret = -1;
			break;
		}else{
			ret = gpio_request(dio_data->gpio_do[i],tmp);
			if(ret != 0){
				printk("[%s]GPIO request error : %s\n",__FUNCTION__,tmp);
				break;
			}
			gpio_direction_output(dio_data->gpio_do[i],SET_OUTPUT(OUTPUT_L,dio_data->oReverse));
		}
	}

	if(ret != 0){
		devm_kfree(&pdev->dev,dio_data);
		printk("Request GPIO output error\n");
		return -EIO;
	}

	if((dio_data->diset==4) && (dio_data->doset==4)){
		adlink_dio_class_DI_DO.class_attrs = adlink_dio_attr_4;
	}
	
	class_register(&adlink_dio_class_DI_DO);
	dio_data->pdev = pdev;

	mutex_init(&dio_data->DIOmutex);
	platform_set_drvdata(pdev,dio_data);
	pdev_dio = pdev;

	
	return 0;
}

static int adlink_DIO_remove(struct platform_device *pdev)
{
	struct adlink_dio_data *dio_data = platform_get_drvdata(pdev);

	kfree(dio_data);	
	return 0;
}


static struct platform_driver adlink_DIO_driver = {

	.probe		= adlink_DIO_probe,
	.remove		= adlink_DIO_remove,
	.driver		= {
		.name	= "adlink_dio",
		.of_match_table = of_match_ptr(adlink_dio_of_match),
	}
};


static int __init adlink_DIO_init(void)
{	
	return platform_driver_register(&adlink_DIO_driver);
}

static void __exit adlink_DIO_exit(void)
{		
	platform_driver_unregister(&adlink_DIO_driver);
}

module_init(adlink_DIO_init);
module_exit(adlink_DIO_exit);

MODULE_AUTHOR("AdlinkTech, Inc.");
MODULE_DESCRIPTION("Adlinktech DIO control driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");
MODULE_ALIAS("platform:adlink-dio");
