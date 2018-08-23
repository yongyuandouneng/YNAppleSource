//
//  ViewController.m
//  test
//
//  Created by ZYN on 2018/8/15.
//  Copyright © 2018年 yongneng. All rights reserved.
//

#import "ViewController.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    NSArray *arr = @[];
    NSString *str = (arr.count - 1) > 0 ? @"1" : @"2";
    NSLog(@"%@", str);
}


- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}


@end
