/**
 * Copyright © 2019 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#include <StGLMesh/StGLUVCylinder.h>

#include <StGLCore/StGLCore20.h>
#include <StGL/StGLContext.h>

StGLUVCylinder::StGLUVCylinder(const StGLVec3& theCenter,
                               const GLfloat theHeight,
                               const GLfloat theRadius,
                               const int theRings)
: StGLMesh(GL_TRIANGLE_STRIP),
  myCenter(theCenter),
  myRadius(theRadius),
  myHeight(theHeight),
  myNbRings(theRings) {
    //
}

StGLUVCylinder::~StGLUVCylinder() {
    //
}

bool StGLUVCylinder::computeMesh() {
    clearRAM();
    if(myNbRings == 0) {
        return false;
    }

    const int aNbVerts = (myNbRings + 1) * 2;
    myVertices.initArray(aNbVerts);
    myNormals .initArray(aNbVerts);
    myTCoords .initArray(aNbVerts);

    for(int aRingIter = 0; aRingIter <= myNbRings; ++aRingIter) {
        const GLfloat aPhi    = float(aRingIter) * float(M_PI * 2.0) / float(myNbRings);
        const GLfloat aTexCrd = float(aRingIter) / float(myNbRings);
        myTCoords.changeValue(aRingIter * 2 + 0) = StGLVec2(aTexCrd, 0.0f);
        myTCoords.changeValue(aRingIter * 2 + 1) = StGLVec2(aTexCrd, 1.0f);

        const StGLVec3 aNorm(cosf(aPhi), 0.0f, sinf(aPhi));
        myNormals.changeValue(aRingIter * 2 + 0) = aNorm;
        myNormals.changeValue(aRingIter * 2 + 1) = aNorm;

        myVertices.changeValue(aRingIter * 2 + 0) = myCenter + aNorm * myRadius - StGLVec3(0.0f, myHeight * 0.5f, 0.0f);
        myVertices.changeValue(aRingIter * 2 + 1) = myCenter + aNorm * myRadius + StGLVec3(0.0f, myHeight * 0.5f, 0.0f);
    }
    return true;
}
