//
//  ViewController.m
//  test
//
//  Created by ZYN on 2018/8/14.
//  Copyright © 2018年 yongneng. All rights reserved.
//

#import "ViewController.h"
#import "YNScrollView.h"
@interface ViewController () <UIScrollViewDelegate>
@property (nonatomic, strong) YNScrollView *scrollView;
@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view, typically from a nib.
    
    self.scrollView = [[YNScrollView alloc] initWithFrame:self.view.bounds];
    self.scrollView.delegate = self;
    //    self.scrollView.scrollEnabled = NO;
    UIView *view = [[UIView alloc]initWithFrame:self.view.bounds] ;
    view.backgroundColor = [UIColor redColor];
    [self.scrollView addSubview:view];
    
    UIPanGestureRecognizer *PAN = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(pan:)];
    [self.scrollView addGestureRecognizer:PAN];
    self.scrollView.contentSize = CGSizeMake(self.view.bounds.size.width * 2, self.view.bounds.size.height);
    self.scrollView.scrollEnabled = NO;
    [self.view addSubview:self.scrollView];
    
}

- (void)pan:(UIPanGestureRecognizer *)PAN {
    
    CGFloat  x= [PAN translationInView:PAN.view].x;
    [self.scrollView setContentOffset:CGPointMake(x, 0) animated:NO];
    
    NSLog(@"== %f",x);
    
}
- (void)scrollViewDidScroll:(UIScrollView *)scrollView {
    NSLog(@"---");
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}


@end
