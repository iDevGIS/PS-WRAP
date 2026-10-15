// PS-WRAP-MAC: หา CAMetalLayer ให้ MoltenVK — ดู include/macMetalLayer.h
// Qt >= 6.10 ให้ layer ของ NSView เป็น QContainerLayer (ไม่ใช่ CAMetalLayer) → vkCreateMetalSurfaceEXT ได้ layer ผิดชนิด
// แล้ว MoltenVK crash ตอนถาม naturalDrawableSizeMVK (upstream chiaki-ng เลยบังคับ OpenGL บน mac ซึ่งทำให้ overlay ตอนสตรีมหาย)
// ทางแก้แบบเดียวกับ azahar/suyu: ถ้า layer ของ view ไม่ใช่ CAMetalLayer ให้หาใน sublayers หรือสร้างใหม่ซ้อนไว้เต็ม view
// ขนาด: MoltenVK ใช้ layer.bounds × contentsScale เป็น drawable → ต้องตั้งตาม view.bounds + backingScaleFactor เอง (autoresizing ของ CALayer ไม่พอ)
#include "macMetalLayer.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <QDebug>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(pswrapMetal, "pswrap.metal")

namespace {

CAMetalLayer *findMetalLayer(NSView *view)
{
	CALayer *root = view.layer;
	if (!root)
		return nil;
	if ([root isKindOfClass:[CAMetalLayer class]])
		return (CAMetalLayer *)root;
	for (CALayer *sub in root.sublayers) {
		if ([sub isKindOfClass:[CAMetalLayer class]])
			return (CAMetalLayer *)sub;
	}
	return nil;
}

void applyGeometry(NSView *view, CAMetalLayer *metal)
{
	const CGFloat scale = view.window ? view.window.backingScaleFactor : (view.layer ? view.layer.contentsScale : 1.0);
	const CGRect bounds = view.bounds;
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	if (metal != (CAMetalLayer *)view.layer)
		metal.frame = bounds;   // sublayer ที่เราสร้าง: ให้เต็ม view เสมอ
	metal.contentsScale = scale;
	metal.drawableSize = CGSizeMake(bounds.size.width * scale, bounds.size.height * scale);
	[CATransaction commit];
	qCDebug(pswrapMetal) << "layer" << (metal == (CAMetalLayer *)view.layer ? "root" : "sublayer")
		<< "view.bounds" << bounds.size.width << "x" << bounds.size.height
		<< "root.bounds" << view.layer.bounds.size.width << "x" << view.layer.bounds.size.height
		<< "scale" << scale << "drawable" << metal.drawableSize.width << "x" << metal.drawableSize.height;
}

} // namespace

void *pswrapMetalLayerForView(void *nsview)
{
	NSView *view = (__bridge NSView *)nsview;
	if (!view)
		return nullptr;
	if (!view.wantsLayer)
		view.wantsLayer = YES;
	if (!view.layer)
		return nullptr;
	CAMetalLayer *metal = findMetalLayer(view);
	if (!metal) {
		metal = [CAMetalLayer layer];
		metal.opaque = YES;
		[view.layer addSublayer:metal];
		qCDebug(pswrapMetal) << "created CAMetalLayer under" << [NSStringFromClass([view.layer class]) UTF8String];
	}
	applyGeometry(view, metal);
	return (__bridge void *)metal;
}

void pswrapMetalLayerSync(void *nsview)
{
	NSView *view = (__bridge NSView *)nsview;
	if (!view || !view.layer)
		return;
	if (CAMetalLayer *metal = findMetalLayer(view))
		applyGeometry(view, metal);
}
