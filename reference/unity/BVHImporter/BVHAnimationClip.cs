using System;
using System.Collections.Generic;
using UnityEngine;

[Serializable]
public class RotationCurve
{
    public Quaternion[] keyFrame;
}

[Serializable]
public class PoseFeature
{
    // Predict 5th,10th,15th,20th,25th,30th frames delta rootmotion, Make vector3.y = 0
    public Vector3[] predictedDeltaRootMotion;
    public float[] predictedDeltaFaceDirectionAngle;
}

[CreateAssetMenu(menuName = "BVHAnimationClip")]
public class BVHAnimationClip : ScriptableObject
{
    public GameObject model; 
    public int totalFrame;
    public float frameTime;
    public List<string> boneList;

    public List<RotationCurve> boneRotations;
    public Vector3 rootBoneOffset;
    public Vector3[] rootMotionCurve;

    public PoseFeature[] poseFeatureData;
}
