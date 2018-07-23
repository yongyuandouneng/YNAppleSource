//
//  main.m
//  test
//
//  Created by ZYN on 2018/7/19.
//

#import <Foundation/Foundation.h>
#import "CFRunLoop.h"

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        // insert code here...
        NSLog(@"Hello, World!");
        
        CFRunLoopRef ref = CFRunLoopGetMain();
        
        
    }
    return 0;
}
