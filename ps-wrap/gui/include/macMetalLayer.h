// PS-WRAP-MAC: CAMetalLayer สำหรับ VkSurface ของ MoltenVK บน Qt >= 6.10 — ดู src/macMetalLayer.mm
#ifndef PSWRAP_MACMETALLAYER_H
#define PSWRAP_MACMETALLAYER_H

// คืน CAMetalLayer* (เป็น void*) ของ NSView — ของ view เอง / ใน sublayers / สร้างใหม่ซ้อนเต็ม view
void *pswrapMetalLayerForView(void *nsview);
// ปรับ frame + contentsScale ของ CAMetalLayer ที่สร้างไว้ให้ตรง view (เรียกตอน resize / ย้ายจอ)
void pswrapMetalLayerSync(void *nsview);

#endif
