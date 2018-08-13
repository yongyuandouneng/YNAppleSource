//
//  ViewController.m
//  YNRunLoopTest
//
//  Created by ZYN on 2018/8/13.
//  Copyright © 2018年 yongneng. All rights reserved.
//

#import "ViewController.h"

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
//    [self performSelector:@selector(onClick:) withObject:@"1"];
//
//    [self performSelector:@selector(onClick:) withObject:@"1" afterDelay:10];
    [self performSelectorOnMainThread:@selector(onClick:) withObject:@"1" waitUntilDone:YES];
    
}
- (IBAction)onClick:(id)sender {
    NSLog(@"---");
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    NSLog(@"---");
}

@end
