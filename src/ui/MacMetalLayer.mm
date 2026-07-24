// MacMetalLayer.mm — macOS Objective-C++ helper for Vulkan surface creation.
//
// Configures a QWidget's NSView with a CAMetalLayer subview so that
// VK_EXT_metal_surface can create a VkSurfaceKHR from it.
// Uses a child NSView (NeurusMetalView) rather than replacing QNSView's
// own layer, which avoids conflicts with Qt's compositing.

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>

// ---------------------------------------------------------------------------
// A thin NSView subclass whose backing layer is always a CAMetalLayer.
// Inserted as a subview of the QNSView so Qt's layer remains untouched.
// ---------------------------------------------------------------------------
@interface NeurusMetalView : NSView
@property (nonatomic, strong) CAMetalLayer* metalLayer;
@end

@implementation NeurusMetalView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        _metalLayer = [CAMetalLayer layer];
        _metalLayer.opaque = YES;
        self.layer = _metalLayer;
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    }
    return self;
}

- (BOOL)isOpaque {
    return YES;
}

@end

// Association key to attach the NeurusMetalView to the parent QNSView.
static const char kMetalViewKey = 0;

extern "C" {

void* makeViewMetalCompatible(void* nativeHandle)
{
    @autoreleasepool {
        NSView* view = (__bridge NSView*)nativeHandle;
        if (!view || ![view isKindOfClass:[NSView class]])
            return nullptr;

        // Check if we already attached a NeurusMetalView
        NeurusMetalView* metalView = objc_getAssociatedObject(view, &kMetalViewKey);
        if (metalView && [metalView isKindOfClass:[NeurusMetalView class]]) {
            // Update scale in case window moved to another display
            CGFloat scale = view.window ? view.window.backingScaleFactor
                                        : [[NSScreen mainScreen] backingScaleFactor];
            metalView.metalLayer.contentsScale = scale;
            return (__bridge void*)metalView.metalLayer;
        }

        // Create a new NeurusMetalView that fills the parent QNSView
        metalView = [[NeurusMetalView alloc] initWithFrame:view.bounds];
        [view addSubview:metalView];

        // Configure the Metal layer
        CAMetalLayer* layer = metalView.metalLayer;
        CGFloat scale = view.window ? view.window.backingScaleFactor
                                    : [[NSScreen mainScreen] backingScaleFactor];
        layer.contentsScale = scale;

        CGSize drawableSize = view.bounds.size;
        drawableSize.width *= scale;
        drawableSize.height *= scale;
        layer.drawableSize = drawableSize;

        // Associate so we can retrieve it later
        objc_setAssociatedObject(view, &kMetalViewKey, metalView,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        return (__bridge void*)layer;
    }
}

void updateMetalLayerSize(void* nativeHandle)
{
    @autoreleasepool {
        NSView* view = (__bridge NSView*)nativeHandle;
        if (!view || ![view isKindOfClass:[NSView class]])
            return;

        NeurusMetalView* metalView = objc_getAssociatedObject(view, &kMetalViewKey);
        if (!metalView)
            return;

        // Resize the subview to match its parent
        metalView.frame = view.bounds;

        CAMetalLayer* layer = metalView.metalLayer;
        CGFloat scale = view.window ? view.window.backingScaleFactor
                                    : [[NSScreen mainScreen] backingScaleFactor];
        layer.contentsScale = scale;

        CGSize drawableSize = view.bounds.size;
        drawableSize.width *= scale;
        drawableSize.height *= scale;
        layer.drawableSize = drawableSize;
    }
}

} // extern "C"
