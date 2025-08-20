
public public void animateTexture(int arg0) {
    if (this.pixels == null) {
        return;
    }
    if (this.animationDirection == 1 || this.animationDirection == 3) {
        if (field1693 == null || field1693.length < this.pixels.length) {
            field1693 = new int[this.pixels.length];
        }
        short var2;
        if (this.pixels.length == 4096) {
            var2 = 64;
        } else {
            var2 = 128;
        }
        int var3 = this.pixels.length;
        int var4 = arg0 * var2 * this.animationSpeed;
        int var5 = var3 - 1;
        if (this.animationDirection == 1) {
            var4 = -var4;
        }
        for (int var6 = 0; var6 < var3; var6++) {
            int var7 = var4 + var6 & var5;
            field1693[var6] = this.pixels[var7];
        }
        int[] var8 = this.pixels;
        this.pixels = field1693;
        field1693 = var8;
    }
    if (this.animationDirection != 2 && this.animationDirection != 4) {
        return;
    }
    if (field1693 == null || field1693.length < this.pixels.length) {
        field1693 = new int[this.pixels.length];
    }
    short var9;
    if (this.pixels.length == 4096) {
        var9 = 64;
    } else {
        var9 = 128;
    }
    int var10 = this.pixels.length;
    int var11 = this.animationSpeed * arg0;
    int var12 = var9 - 1;
    if (this.animationDirection == 2) {
        var11 = -var11;
    }
    for (int var13 = 0; var13 < var10; var13 += var9) {
        for (int var14 = 0; var14 < var9; var14++) {
            int var15 = var13 + var14;
            int var16 = (var11 + var14 & var12) + var13;
            field1693[var15] = this.pixels[var16];
        }
    }
    int[] var17 = this.pixels;
    this.pixels = field1693;
    field1693 = var17;
} 