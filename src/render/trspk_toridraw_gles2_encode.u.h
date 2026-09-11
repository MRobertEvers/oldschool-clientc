/* Ordered packed encoding shared by reference and word-store variants. */
void TRSPK_GLES2_ENCODER(
    const struct ToriDraw_Model* model,const int* order,uint32_t count,
    const float* world_xyz,struct TRSPK_VertexGLES2* out)
{
    assert(!model->face_textures);
    for(uint32_t i=0;i<count;i++,out+=3) {
        uint32_t f=(uint32_t)order[i];
        if(f>=(uint32_t)model->face_count) {
            for(unsigned k=0;k<3;k++) {
                out[k].position[0]=out[k].position[1]=out[k].position[2]=0.0f;
                out[k].rgba=0;out[k].texcoord[0]=out[k].texcoord[1]=0.5f;
                TRSPK_GLES2_METADATA(&out[k]);
            }
            continue;
        }
        uint32_t alpha=model->face_alphas ? 255u-model->face_alphas[f] : 255u;
        hsl16_t c=model->face_colors_c[f];
        if(alpha<=1u || c==TORIDRAWHSL16_HIDDEN)alpha=0;
        alpha<<=24;
        uint32_t colors[3];colors[0]=TRSPK_GLES2_COLOR(model->face_colors_a[f],alpha);
        if(c==TORIDRAWHSL16_FLAT || c==TORIDRAWHSL16_HIDDEN)colors[1]=colors[2]=colors[0];
        else {
            colors[1]=TRSPK_GLES2_COLOR(model->face_colors_b[f],alpha);
            colors[2]=TRSPK_GLES2_COLOR(c,alpha);
        }
        uint32_t indices[3]={(uint32_t)model->face_indices_a[f],(uint32_t)model->face_indices_b[f],(uint32_t)model->face_indices_c[f]};
        for(unsigned k=0;k<3;k++) {
            const float* xyz=world_xyz+indices[k]*3u;
            out[k].position[0]=xyz[0];out[k].position[1]=xyz[1];out[k].position[2]=xyz[2];
            out[k].rgba=colors[k];out[k].texcoord[0]=out[k].texcoord[1]=0.5f;
            TRSPK_GLES2_METADATA(&out[k]);
        }
    }
}
