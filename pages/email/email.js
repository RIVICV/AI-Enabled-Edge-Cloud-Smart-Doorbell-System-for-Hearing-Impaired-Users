const API_BASE = 'http://114.132.168.16:5000'

Page({
  data: {
    email: '',
    emailHint: ''
  },

  onLoad() {
    this.loadEmail()
  },

  // ===== 加载当前邮箱 =====
  loadEmail() {
    const that = this
    wx.request({
      url: API_BASE + '/api/email',
      method: 'GET',
      success: (res) => {
        if (res.data && res.data.code === 0) {
          that.setData({ email: res.data.email || '' })
        }
      },
      fail: () => {
        console.log('加载邮箱失败')
      }
    })
  },

  // ===== 输入框变化 =====
  onEmailInput(e) {
    const email = e.detail.value.trim()
    this.setData({ email })
    
    // 实时验证邮箱格式
    if (email && !this.isValidEmail(email)) {
      this.setData({ emailHint: '⚠️ 请输入正确的邮箱格式' })
    } else {
      this.setData({ emailHint: '' })
    }
  },

  // ===== 验证邮箱格式 =====
  isValidEmail(email) {
    const regex = /^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$/
    return regex.test(email)
  },

  // ===== 保存邮箱 =====
  saveEmail() {
    const email = this.data.email.trim()
    
    if (!email) {
      wx.showToast({ title: '请输入邮箱地址', icon: 'none' })
      return
    }

    if (!this.isValidEmail(email)) {
      wx.showToast({ title: '邮箱格式不正确', icon: 'none' })
      return
    }

    wx.showLoading({ title: '保存中...' })

    wx.request({
      url: API_BASE + '/api/email',
      method: 'POST',
      data: { email: email },
      success: (res) => {
        wx.hideLoading()
        if (res.data && res.data.code === 0) {
          wx.showToast({ title: '✅ 保存成功', icon: 'success' })
          // 返回上一页
          setTimeout(() => {
            wx.navigateBack()
          }, 500)
        } else {
          wx.showToast({ title: res.data.message || '保存失败', icon: 'none' })
        }
      },
      fail: () => {
        wx.hideLoading()
        wx.showToast({ title: '网络请求失败', icon: 'none' })
      }
    })
  },

  // ===== 发送测试邮件 =====
  testEmail() {
    const email = this.data.email.trim()
    
    if (!email) {
      wx.showToast({ title: '请先保存邮箱', icon: 'none' })
      return
    }

    wx.showLoading({ title: '发送测试中...' })

    wx.request({
      url: API_BASE + '/api/email/test',
      method: 'POST',
      data: { email: email },
      success: (res) => {
        wx.hideLoading()
        if (res.data && res.data.code === 0) {
          wx.showToast({ title: '✅ 测试邮件已发送', icon: 'success' })
        } else {
          wx.showToast({ title: res.data.message || '发送失败', icon: 'none' })
        }
      },
      fail: () => {
        wx.hideLoading()
        wx.showToast({ title: '网络请求失败', icon: 'none' })
      }
    })
  }
})