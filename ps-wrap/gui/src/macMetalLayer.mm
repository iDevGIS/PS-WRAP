// PS-WRAP-MAC: หา CAMetalLayer ให้ MoltenVK — ดู include/macMetalLayer.h
// Qt >= 6.10 ให้ layer ของ NSView เป็น QContainerLayer (ไม่ใช่ CAMetalLayer) → vkCreateMetalSurfaceEXT ได้ layer ผิดชนิด
// แล้ว MoltenVK crash ตอนถาม naturalDrawableSizeMVK (upstream chiaki-ng เลยบังคับ OpenGL บน mac ซึ่งทำให้ overlay ตอนสตรีมหาย)
// ทางแก้แบบเดียวกับ azahar/suyu: ถ้า layer ของ view ไม่ใช่ CAMetalLayer ให้หาใน sublayers หรือสร้างใหม่ซ้อนไว้เต็ม view
#include "macMetalLayer.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

void *pswrapMetalLayerForView(void *nsview)
{
	NSView *view = (__bridge NSView *)nsview;
	if (!view)
		return nullptr;
	if (!view.wantsLayer)
		view.wantsLayer = YES;
	CALayer *root = view.layer;
	if (!root)
		return nullptr;
	if ([root isKindOfClass:[CAMetalLayer class]])
		return (__bridge void *)root;
	for (CALayer *sub in root.sublayers) {
		if ([sub isKindOfClass:[CAMetalLayer class]])
			return (__bridge void *)sub;
	}
	CAMetalLayer *metal = [CAMetalLayer layer];
	metal.frame = root.bounds;
	metal.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;   // ตาม view ตอน resize — MoltenVK ใช้ bounds × contentsScale เป็นขนาด drawable
	metal.contentsScale = view.window ? view.window.backingScaleFactor : root.contentsScale;
	metal.opaque = YES;
	[root addSublayer:metal];
	return (__bridge void *)metal;
}

void pswrapMetalLayerSync(void *nsview)
{
	NSView *view = (__bridge NSView *)nsview;
	if (!view || !view.layer)
		return;
	CALayer *root = view.layer;
	if ([root isKindOfClass:[CAMetalLayer class]])
		return;
	for (CALayer *sub in root.sublayers) {
		if (![sub isKindOfClass:[CAMetalLayer class]])
			continue;
		[CATransaction begin];
		[CATransaction setDisableActions:YES];
		sub.frame = root.bounds;
		if (view.window)
			sub.contentsScale = view.window.backingScaleFactor;
		[CATransaction commit];
	}
}
