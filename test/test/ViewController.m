//
//  ViewController.m
//  test
//
//  Created by ZYN on 2018/7/24.
//  Copyright © 2018年 yongneng. All rights reserved.
//

#import "ViewController.h"
#import "People.h"


@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    NSObject *obj = [[NSObject alloc] init];
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @synchronized(obj) {
            NSLog(@"需要线程同步的操作1 开始");
            sleep(3);
            NSLog(@"需要线程同步的操作1 结束");
        }
    });
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
//        sleep(1);
        NSLog(@"---");
        @synchronized(obj) {
        }
        
        NSLog(@"需要线程同步的操作2");
        NSLog(@"---");
    });
    
}
- (IBAction)pop:(id)sender {
    [self.navigationController popToRootViewControllerAnimated:YES];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    
    NSLog(@"viewWillDisappear -- %@", self);
}

- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    NSLog(@"viewDidDisappear -- %@", self);
}

- (void)dealloc {
    NSLog(@"dealloc");
}


@end
