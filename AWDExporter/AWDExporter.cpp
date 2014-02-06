#include <maya/MStatus.h>
#include <maya/MObject.h>
#include <maya/MFnPlugin.h>
#include <maya/MString.h>
#include <maya/MVector.h>
#include <maya/MStringArray.h>
#include <maya/MPxFileTranslator.h>
#include <maya/MGlobal.h>
#include <maya/MItDag.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MItSelectionList.h>
#include <maya/MSelectionList.h>
#include <maya/MIOStream.h>
#include <maya/MFStream.h>
#include <maya/MFileIO.h>
#include <maya/MFnTransform.h>
#include <maya/MNamespace.h>
#include <maya/MFnMesh.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MPointArray.h>
#include <string.h>
#include <vector>

#include "awd.h"
#include "geomutil.h"

using namespace std;

//This is the backbone for creating a MPxFileTranslator
class AWDExporter : public MPxFileTranslator {
public:

    //Constructor
    AWDExporter () {};
    //Destructor
    virtual            ~AWDExporter () {};

    //This tells maya that the translator can read files.
    //Basically, you can import or load with your translator.
    bool haveReadMethod() const { return false; }

    //This tells maya that the translator can write files.
    //Basically, you can export or save with your translator.
    bool haveWriteMethod() const { return true; }

    //If this method returns true, and the awd file is referenced in a scene, the write method will be
    //called when a write operation is performed on the parent file.  This use is for users who wish
    //to implement a custom file referencing system.
    //For this example, we will return false as we will use Maya's file referencing system.
    bool haveReferenceMethod() const { return false; }

    //If this method returns true, it means we support namespaces.
    bool haveNamespaceSupport()    const { return true; }

    //This method is used by Maya to create instances of the translator.
    static void* creator();
    
    //This returns the default extension ".awd" in this case.
    MString defaultExtension () const;

    //If this method returns true it means that the translator can handle opening files 
    //as well as importing them.
    //If the method returns false then only imports are handled. The difference between 
    //an open and an import is that the scene is cleared(e.g. 'file -new') prior to an 
    //open, which may affect the behaviour of the translator.
    bool canBeOpened() const { return false; }

    //This function is called by maya when export or save is called.
    MStatus writer ( const MFileObject& file,
                                        const MString& optionsString,
                             MPxFileTranslator::FileAccessMode mode);

private:
    static MString const magic;
    
    int getMeshes();
    vector<unsigned char> long2char(unsigned long val);
    
    AWDTriGeom * ExportTriObject(MObject *mesh);
    
    AWD* awd;

};

//Creates one instance of the AWDExporter
void* AWDExporter::creator()
{
    return new AWDExporter();
}

// Initialize our magic string
MString const AWDExporter::magic("AWD");

//The writer simply goes gathers all objects from the scene.
//We will check if the object has a transform, if so, we will check
//if it's either a nurbsSphere, nurbsCone or nurbsCylinder.  If so,
//we will write it out.
MStatus AWDExporter::writer ( const MFileObject& file,
                                const MString& options,
                                MPxFileTranslator::FileAccessMode mode)
{
    MGlobal::displayInfo("writer");
    MStatus status;
	const MString fname = file.fullName();
    
    MGlobal::displayInfo("saving file");

    awd = new AWD(UNCOMPRESSED, 0);
    
    MGlobal::displayInfo("awd instance created");

    
    int fd = open(fname.asChar(), O_TRUNC | O_CREAT | O_RDWR, S_IWRITE);
    
    MGlobal::displayInfo("file open: ");
    MString fdId = MString(to_string(fd).c_str());
    MGlobal::displayInfo(fdId);
    
    if (fd == -1) return MS::kFailure;
    
    getMeshes();
    
    MGlobal::displayInfo("let's flush!");
    awd->flush(fd);
    
    return MS::kSuccess;
}

int AWDExporter::getMeshes()
{
    MItDag it(MItDag::kDepthFirst, MFn::kMesh);
    
    while (!it.isDone())
    {
        MObject obj = it.item();
        MFnMesh fn(it.item());
        
        if (!fn.isIntermediateObject())
        {
            std::cout << "Mesh " << fn.name().asChar() << endl;
            AWDTriGeom *mesh;
            
            mesh = ExportTriObject(&obj);
            
            if (mesh == NULL) return NULL;
            
            AWDMeshInst *inst = new AWDMeshInst(fn.name().asChar(), strlen(fn.name().asChar()), mesh);
            
            awd->add_scene_block(inst);
        }
        
        it.next();
    }
    
    return TRUE;
}

AWDTriGeom* AWDExporter::ExportTriObject(MObject *mesh)
{
    AWDTriGeom *awdGeom;
    
    MFnMesh fn(*mesh);
    
    MIntArray triangleCounts;
    MIntArray triangleVertices;
    
    MPointArray vts;
    MFloatVectorArray nrmls;
    
    MFloatArray uvs_u;
    MFloatArray uvs_v;
    
    fn.getPoints(vts);
    fn.getTriangles(triangleCounts, triangleVertices);
    fn.getNormals(nrmls);
    fn.getUVs(uvs_u, uvs_v);
    
    AWDGeomUtil util;
    util.include_normals = true;
    util.include_uv = true;
    
    for (int i = 0; i < triangleCounts.length(); i++)
    {
        for (int j = 0; j < 3; j++)
        {
            MPoint vtx = vts[i * 3 + j];
            MFloatVector nrml = nrmls[i * 3 + j];
            
            vdata *vd = (vdata *)malloc(sizeof(vdata));
            vd->x = vtx.x;
            vd->y = vtx.y;
            vd->z = vtx.z;
            
            vd->nx = nrml.x;
            vd->ny = nrml.y;
            vd->nz = nrml.z;
            
            vd->u = uvs_u[i * 3 + j];
            vd->v = uvs_v[i * 3 + j];
            
            util.append_vdata_struct(vd);
        }
        
    }
    
    char *name = (char *)malloc(strlen(fn.name().asChar()) + 6);
    strcpy(name, fn.name().asChar());
    strcat(name, "_geom");
    
    awdGeom = new AWDTriGeom(name, strlen(name));
    util.build_geom(awdGeom);
    
    awd->add_mesh_data(awdGeom);
    
    return awdGeom;
}

vector<unsigned char> AWDExporter::long2char(unsigned long val)
{
    vector<unsigned char> bytes;
    
    bytes[0] = (val >> 24) & 0xFF;
    bytes[1] = (val >> 16) & 0xFF;
    bytes[2] = (val >> 8) & 0xFF;
    bytes[3] = val & 0xFF;
    
    return bytes;
}

// Whenever Maya needs to know the preferred extension of this file format,
// it calls this method. For example, if the user tries to save a file called
// "test" using the Save As dialog, Maya will call this method and actually
// save it as "test.lep". Note that the period should *not* be included in
// the extension.
MString AWDExporter::defaultExtension () const
{
    return "awd";
}

MStatus initializePlugin( MObject obj )
{
    MStatus   status;
    MFnPlugin plugin( obj, PLUGIN_COMPANY, "3.0", "Any");

    // Register the translator with the system
    // The last boolean in this method is very important.
    // It should be set to true if the reader method in the derived class
    // intends to issue MEL commands via the MGlobal::executeCommand 
    // method.  Setting this to true will slow down the creation of
    // new objects, but allows MEL commands other than those that are
    // part of the Maya Ascii file format to function correctly.
    status =  plugin.registerFileTranslator( "AWD",
                                        "",
                                        AWDExporter::creator,
                                        "",
                                        "",
                                        true );
    if (!status) 
    {
        status.perror("registerFileTranslator");
        return status;
    }

    return status;
}

MStatus uninitializePlugin( MObject obj )
{
    MStatus   status;
    MFnPlugin plugin( obj );

    status =  plugin.deregisterFileTranslator( "AWD" );
    if (!status)
    {
        status.perror("deregisterFileTranslator");
        return status;
    }

    return status;
}

