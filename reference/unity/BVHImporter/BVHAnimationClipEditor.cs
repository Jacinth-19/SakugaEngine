using UnityEditor;
using UnityEngine;

[CustomEditor(typeof(BVHAnimationClip))]
public class BVHAnimationClipEditor : Editor
{
    private BVHAnimationClip animationClip;
    
    public override void OnInspectorGUI()
    {
        animationClip = (BVHAnimationClip)target;
        
        DrawDefaultInspector();

        if (GUILayout.Button("GeneratePoseFeature"))
        {
            GeneratePoseFeatureData();
        }
    }

    private void GeneratePoseFeatureData()
    {
        animationClip.poseFeatureData = new PoseFeature[animationClip.totalFrame];
        
        for (int frameIndex = 0; frameIndex < animationClip.totalFrame; frameIndex++)
        {
            PoseFeature currentFramePoseFeature = new PoseFeature();
            currentFramePoseFeature.predictedDeltaRootMotion = new Vector3[6];
            currentFramePoseFeature.predictedDeltaFaceDirectionAngle = new float[6];
            
            
            Vector3 currentFrameRootMotion = animationClip.rootMotionCurve[frameIndex];
            Vector3 currentFrameFaceDirection = animationClip.boneRotations[0].keyFrame[frameIndex] * Vector3.forward;
            
            for (int predictIndex = 1; predictIndex <= 6; predictIndex++)
            {
                if (frameIndex + 30 >= animationClip.totalFrame)
                {
                    currentFramePoseFeature.predictedDeltaRootMotion[predictIndex - 1] = Vector3.positiveInfinity;
                    currentFramePoseFeature.predictedDeltaFaceDirectionAngle[predictIndex - 1] = 0;
                }
                else
                {
                    //predict rootmotion
                    Vector3 predictedDeltaRootMotion = animationClip.rootMotionCurve[frameIndex + predictIndex * 5] - currentFrameRootMotion;
                    predictedDeltaRootMotion.y = 0;
                    currentFramePoseFeature.predictedDeltaRootMotion[predictIndex - 1] = predictedDeltaRootMotion;
                    
                    //predict face delta face direction
                    Vector3 predictedFaceDirection = animationClip.boneRotations[0].keyFrame[frameIndex + predictIndex * 5] * Vector3.forward;
                    float deltaAngle = Vector3.SignedAngle(currentFrameFaceDirection, predictedFaceDirection, Vector3.up);
                    currentFramePoseFeature.predictedDeltaFaceDirectionAngle[predictIndex - 1] = deltaAngle;
                }
            }
            

            animationClip.poseFeatureData[frameIndex] = currentFramePoseFeature;
        }
    }
}
