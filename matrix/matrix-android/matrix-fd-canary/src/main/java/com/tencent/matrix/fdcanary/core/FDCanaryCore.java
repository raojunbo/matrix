package com.tencent.matrix.fdcanary.core;

import com.tencent.matrix.fdcanary.FDCanaryPlugin;
import com.tencent.matrix.fdcanary.config.FDConfig;
import com.tencent.matrix.fdcanary.util.FDCanaryUtil;
import com.tencent.matrix.report.Issue;
import com.tencent.matrix.report.IssuePublisher;

import java.util.List;


public class FDCanaryCore implements OnJniIssuePublishListener, IssuePublisher.OnIssueDetectListener {
    private static final String TAG = "Matrix.FDCanaryCore";

    private final FDConfig               mFDConfig;

    private final FDCanaryPlugin mFDCanaryPlugin;

    private boolean           mIsStart;

    public FDCanaryCore(FDCanaryPlugin mFDCanaryPlugin) {
        mFDConfig = mFDCanaryPlugin.getConfig();
        this.mFDCanaryPlugin = mFDCanaryPlugin;
    }

    public void start() {
        initDetectorsAndHookers(mFDConfig);
        synchronized (this) {
            mIsStart = true;
        }
    }

    public synchronized boolean isStart() {
        return mIsStart;
    }

    public void stop() {
        synchronized (this) {
            mIsStart = false;
        }


        FDCanaryJniBridge.uninstall();
    }

    @Override
    public void onDetectIssue(Issue issue) {
        mFDCanaryPlugin.onDetectIssue(issue);
    }

    private void initDetectorsAndHookers(FDConfig ioConfig) {
        assert ioConfig != null;

        if (ioConfig.isDetectFileIOInMainThread()
                || ioConfig.isDetectFileIOBufferTooSmall()
                || ioConfig.isDetectFileIORepeatReadSameFile()) {
            FDCanaryJniBridge.install(ioConfig, this);
        }

    }

    @Override
    public void onIssuePublish(List<FDIssue> issues) {
        if (issues == null) {
            return;
        }

        for (int i = 0; i < issues.size(); i++) {
            mFDCanaryPlugin.onDetectIssue(FDCanaryUtil.convertFDIssueToReportIssue(issues.get(i)));
        }
    }
}

