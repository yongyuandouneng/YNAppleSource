//
//  main.m
//  Runloop
//
//  Created by ZYN on 2018/7/19.
//  Copyright © 2018年 yongneng. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "CFRunLoop.h"

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        // insert code here...
        NSLog(@"Hello, World!");
        CFRunLoopRef runloop = CFRunLoopGetCurrent();
    }
    return 0;
}
