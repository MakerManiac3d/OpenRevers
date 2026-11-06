// SPDX-License-Identifier: GPL-3.0-or-later
// Not part of the build: makes tests/data/e57 with libE57Format (see the README there).
// Writes the E57 fixtures for OpenRevers' reader tests with the reference library.
#include "E57SimpleWriter.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
static void spiral(int i, int n, double& x, double& y, double& z) {
    double t = (i + 0.5) / n, zz = 1 - 2 * t, r = std::sqrt(1 - zz * zz), ph = i * 2.39996323;
    x = r * std::cos(ph); y = r * std::sin(ph); z = zz;
}
int main(int, char** argv) {
    std::string dir = argv[1];
    e57::WriterOptions wo; wo.guid = "{OpenRevers-test}";
    { // A: float cartesian with normals
        e57::Writer w(dir + "/float_normals.e57", wo);
        e57::Data3D h; h.guid = "{A}"; h.name = "A";
        const int n = 8000; h.pointCount = n;
        h.pointFields.cartesianXField = h.pointFields.cartesianYField = h.pointFields.cartesianZField = true;
        h.pointFields.normalXField = h.pointFields.normalYField = h.pointFields.normalZField = true;
        h.pointFields.pointRangeNodeType = e57::NumericalNodeType::Float;
        h.pointFields.pointRangeMinimum = -1e6; h.pointFields.pointRangeMaximum = 1e6;
        e57::Data3DPointsFloat d(h);
        for (int i = 0; i < n; ++i) { double x,y,z; spiral(i,n,x,y,z); d.cartesianX[i]=50*x; d.cartesianY[i]=50*y; d.cartesianZ[i]=50*z; d.normalX[i]=x; d.normalY[i]=y; d.normalZ[i]=z; }
        w.WriteData3DData(h, d);
    }
    { // B: scaled integers, pose, invalid points, intensity and colour
        e57::Writer w(dir + "/scaled_pose.e57", wo);
        e57::Data3D h; h.guid = "{B}"; h.name = "B";
        const int n = 6000; h.pointCount = n;
        auto& f = h.pointFields;
        f.cartesianXField = f.cartesianYField = f.cartesianZField = f.cartesianInvalidStateField = true;
        f.intensityField = true; f.colorRedField = f.colorGreenField = f.colorBlueField = true;
        f.pointRangeNodeType = e57::NumericalNodeType::ScaledInteger; f.pointRangeScale = 0.0001;
        f.pointRangeMinimum = -45; f.pointRangeMaximum = 45;
        h.intensityLimits.intensityMinimum = 0; h.intensityLimits.intensityMaximum = 1;
        h.colorLimits.colorRedMinimum = 0; h.colorLimits.colorRedMaximum = 255;
        h.colorLimits.colorGreenMinimum = 0; h.colorLimits.colorGreenMaximum = 255;
        h.colorLimits.colorBlueMinimum = 0; h.colorLimits.colorBlueMaximum = 255;
        h.pose.rotation.w = std::sqrt(0.5); h.pose.rotation.z = std::sqrt(0.5);
        h.pose.translation.x = 100;
        e57::Data3DPointsDouble d(h);
        for (int i = 0; i < n; ++i) {
            double x,y,z; spiral(i,n,x,y,z);
            d.cartesianX[i]=40*x; d.cartesianY[i]=40*y; d.cartesianZ[i]=40*z;
            d.cartesianInvalidState[i] = i % 10 == 0 ? 2 : 0;
            if (i % 10 == 0) d.cartesianX[i] = d.cartesianY[i] = d.cartesianZ[i] = 0;
            d.intensity[i] = (i % 100) / 100.0; d.colorRed[i] = i % 256; d.colorGreen[i] = 7; d.colorBlue[i] = 200;
        }
        w.WriteData3DData(h, d);
    }
    { // C: two scans in double spherical coordinates, each moved
        e57::Writer w(dir + "/spherical_two_scans.e57", wo);
        for (int s = 0; s < 2; ++s) {
            e57::Data3D h; h.guid = s ? "{C2}" : "{C1}"; h.name = "C";
            const int n = 3000; h.pointCount = n;
            auto& f = h.pointFields;
            f.sphericalRangeField = f.sphericalAzimuthField = f.sphericalElevationField = true;
            f.pointRangeNodeType = e57::NumericalNodeType::Double; f.angleNodeType = e57::NumericalNodeType::Double;
            f.pointRangeMinimum = 0; f.pointRangeMaximum = 1e6; f.angleMinimum = -10; f.angleMaximum = 10;
            h.pose.translation.y = s * 200.0;
            e57::Data3DPointsDouble d(h);
            for (int i = 0; i < n; ++i) {
                double x,y,z; spiral(i,n,x,y,z);
                d.sphericalRange[i] = 30; d.sphericalAzimuth[i] = std::atan2(y, x); d.sphericalElevation[i] = std::asin(z);
            }
            w.WriteData3DData(h, d);
        }
    }
    std::puts("ok");
}
