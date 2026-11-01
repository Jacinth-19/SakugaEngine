using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;

[CustomEditor(typeof(BVHAnimationImporter))]
public class BVHAnimationImporterEditor : Editor
{
    private BVHParser bvhParser;
    private BVHAnimationClip bvhAnimationClip;
    private BVHAnimationImporter bvhImporter;
    
    public override void OnInspectorGUI()
    {
        DrawDefaultInspector();

        bvhImporter = (BVHAnimationImporter)target;

        if (GUILayout.Button("Import animation"))
        {
            parseFile(bvhImporter.filename);
        }
        
        if (GUILayout.Button("Import animation in directory"))
        {
            string[] files = Directory.GetFiles(bvhImporter.directory);
            foreach (var file in files)
            {
                if (file.EndsWith(".bvh"))
                {
                    parseFile(file);
                }
            }
        }
    }

    public void parseFile(string fileName) {
        bvhAnimationClip = ScriptableObject.CreateInstance<BVHAnimationClip>();
        bvhAnimationClip.boneList = new List<string>();
        bvhAnimationClip.boneRotations = new List<RotationCurve>();

        string shortFileName = fileName.Split("/").Last().Split("\\").Last().Replace(".bvh", ".asset");
        string fullFileName = bvhImporter.outputDirectory + "/" + shortFileName;
        AssetDatabase.CreateAsset(bvhAnimationClip, fullFileName);
        
        parse(File.ReadAllText(fileName));
        
        EditorUtility.SetDirty(bvhAnimationClip);
        AssetDatabase.SaveAssets();
    }

    public void parse(string bvhData)
    {
        bvhParser = new BVHParser(bvhData);
        ConvertToBVHAnimationClip();
    }

    private void ConvertToBVHAnimationClip()
    {
        bvhAnimationClip.totalFrame = bvhParser.frames;
        
        List<BVHParser.BVHBone> boneList = bvhParser.boneList;
        for (int boneIndex = 0; boneIndex < boneList.Count; boneIndex++)
        {
            BVHParser.BVHBone boneInfo = boneList[boneIndex];
            bvhAnimationClip.boneList.Add(boneInfo.name);

            //Convert RootMotion
            if (boneIndex == 0 && boneInfo.channels[0].enabled && boneInfo.channels[1].enabled &&
                boneInfo.channels[2].enabled)
            {
                bvhAnimationClip.rootBoneOffset = new Vector3(-bvhParser.root.offsetX / 100f,
                    bvhParser.root.offsetY / 100f, bvhParser.root.offsetZ / 100f);
                bvhAnimationClip.rootMotionCurve = new Vector3[bvhParser.frames];
                for (int frame = 0; frame < bvhParser.frames; frame++)
                {
                    Vector3 position = new Vector3(-boneInfo.channels[0].values[frame] / 100f,
                        boneInfo.channels[1].values[frame] / 100f, boneInfo.channels[2].values[frame] / 100f);
                    bvhAnimationClip.rootMotionCurve[frame] = position;
                }
            }
           
            //Convert bone rotation
            if (boneInfo.channels[3].enabled && boneInfo.channels[4].enabled && boneInfo.channels[5].enabled)
            {
                bvhAnimationClip.boneRotations.Add(new RotationCurve());
                bvhAnimationClip.boneRotations[boneIndex].keyFrame = new Quaternion[bvhParser.frames];
                    
                for (int frame = 0; frame < bvhParser.frames; frame++)
                {
                    Vector3 eulerBVH = new Vector3(wrapAngle(boneInfo.channels[3].values[frame]),
                        wrapAngle(boneInfo.channels[4].values[frame]), wrapAngle(boneInfo.channels[5].values[frame]));
                    Quaternion rotation = fromEulerZYX(eulerBVH);
                    rotation.y = -rotation.y;
                    rotation.z = -rotation.z;
                    bvhAnimationClip.boneRotations[boneIndex].keyFrame[frame] = rotation;
                }
            }
        }
    }
    
    private float wrapAngle(float a) {
        if (a > 180f) {
            return a - 360f;
        }
        if (a < -180f) {
            return 360f + a;
        }
        return a;
    }
    
    private Quaternion fromEulerZYX(Vector3 euler) {
        return Quaternion.AngleAxis(euler.z, Vector3.forward) * Quaternion.AngleAxis(euler.y, Vector3.up) * Quaternion.AngleAxis(euler.x, Vector3.right);
    }
}
