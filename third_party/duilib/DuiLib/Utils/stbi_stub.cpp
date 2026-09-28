// Real stb_image decoder for DuiLib CRenderEngine::LoadImage.
// Root cause fix: the P0 null stub made stbi_load_from_memory always return nullptr,
// so DuiLib never displayed foreimage BMPs (FastFile thumbnails/icons).
extern "C" {
#include "stb_image.c"
}
