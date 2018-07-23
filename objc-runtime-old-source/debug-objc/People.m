//
//  People.m
//  debug-objc
//
//  Created by ZYN on 2018/7/16.
//

#import "People.h"

@implementation People

- (void)dealloc {
    NSLog(@"---People 调用析构函数");
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        
    }
    return self;
}

@end
