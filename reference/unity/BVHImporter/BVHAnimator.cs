using System.Linq;
using UnityEngine;

public class BVHAnimator : MonoBehaviour
{
    public BVHAnimationClip animationClip;
    
    public int currentFrameDisplay;

    private Transform[] m_BoneList;
    private int m_CurrentFrame;
    private float m_Timer;
    private Transform rootBone;

    private Quaternion[] m_CurrentPose;
    private Quaternion[] m_TargetPose;
    private Vector3 m_CurrentRootMotion;
    private Vector3 m_TargetRootMotion;

    private float m_TimerBeforeLastSample;
    // Start is called before the first frame update
    void Start()
    {
        rootBone = transform.Find("Model:Hips");
        m_BoneList = new Transform[animationClip.boneList.Count];
        m_CurrentPose = new Quaternion[animationClip.boneList.Count];
        m_TargetPose = new Quaternion[animationClip.boneList.Count];
        
        if (animationClip != null)
        {
            var bones = transform.GetComponentsInChildren<Transform>().ToList();
            for (int i = 0; i < animationClip.boneList.Count; i++)
            {
                var boneName = animationClip.boneList[i];
                Transform bone = bones.Find((boneTransform) => { return boneTransform.name.Contains(boneName); });
                if (bone != null)
                {
                    m_BoneList[i] = bone;
                }
            }
        }
        
        SampleAnimation();
    }

    // Update is called once per frame
    void Update()
    {
        if (m_Timer > (1f / 30f))
        {
            SampleAnimation();
            m_CurrentFrame++;
            currentFrameDisplay = m_CurrentFrame;
            m_Timer -= (1f / 30f);
        }
        m_Timer += Time.deltaTime;

        m_TimerBeforeLastSample += Time.deltaTime;
        TickPose();

        // Debug.DrawLine(rootBone.transform.position, rootBone.transform.position + rootBone.forward, Color.green);
    }

    private void SampleAnimation()
    {
        m_TargetPose.CopyTo(m_CurrentPose, 0);
        m_CurrentRootMotion = m_TargetRootMotion;
        for (int i = 0; i < m_BoneList.Length; i++)
        {
            m_TargetPose[i] = animationClip.boneRotations[i].keyFrame[m_CurrentFrame];
        }

        m_TargetRootMotion = animationClip.rootMotionCurve[m_CurrentFrame];
        
        m_TimerBeforeLastSample = 0;
        // Debug.Log("Sample Animation");
    }

    private void TickPose()
    {
        float lerpPercent = m_TimerBeforeLastSample * 30f;
        for (int i = 0; i < m_TargetPose.Length; i++)
        {
            m_BoneList[i].localRotation = Quaternion.Lerp(m_CurrentPose[i], m_TargetPose[i], lerpPercent);
        }

        transform.position = Vector3.Lerp(m_CurrentRootMotion, m_TargetRootMotion, lerpPercent) - animationClip.rootBoneOffset;
        Debug.Log(lerpPercent);
    }
}
