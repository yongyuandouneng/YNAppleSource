//
//  ViewController.m
//  YNRunLoopTest
//
//  Created by ZYN on 2018/8/13.
//  Copyright © 2018年 yongneng. All rights reserved.
//

#import "ViewController.h"
#import "People.h"

typedef void(^BlockA)(void);

@interface ViewController ()

@property (nonatomic, weak) void (^blockA)();
@property (nonatomic, copy) NSArray *blocks;
@end

@implementation ViewController


- (void)viewDidLoad {
    
    [super viewDidLoad];
    
//    NSString *string = @"boy";
//
//    _blocks = [[NSArray alloc] initWithObjects:^{NSLog(@"HelloWorld, %@", string);}, ^{NSLog(@"Hello2World, %@", string);}, ^{NSLog(@"HelloWorld");}, nil];
    
}

- (IBAction)onClick:(id)sender {
    
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    for (BlockA blockA in _blocks) {
        NSLog(@"%@", [blockA class]);
        blockA();
    }
}

@end
