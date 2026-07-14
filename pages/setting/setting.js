const API_BASE = 'http://114.132.168.16:5000'

Page({
  data: {
    serverUrl: API_BASE,
    totalCount: 0,
    todayCount: 0,
    emergencyEmail: ''
  },

  onShow() {
    this.loadStats()
    this.loadEmail()
  },

  loadEmail() {
    const that = this
    wx.request({
      url: API_BASE + '/api/email',
      method: 'GET',
      success: (res) => {
        if (res.data && res.data.code === 0) {
          that.setData({ emergencyEmail: res.data.email || '' })
        }
      },
      fail: () => {
        console.log('加载邮箱失败')
      }
    })
  },

  goToEmailSetting() {
    wx.navigateTo({ url: '/pages/email/email' })
  },

  parseDate(dateStr) {
    if (typeof dateStr === 'string' && dateStr.includes('-')) {
      dateStr = dateStr.replace(/-/g, '/')
    }
    return new Date(dateStr)
  },

  loadStats() {
    const that = this
    wx.request({
      url: API_BASE + '/api/history?limit=100',
      method: 'GET',
      success: (res) => {
        if (res.data && res.data.code === 0) {
          const records = res.data.data || []
          that.setData({
            totalCount: records.length
          })
          that.calculateTodayCount(records)
        }
      },
      fail: () => {
        console.log('加载统计失败')
      }
    })
  },

  calculateTodayCount(records) {
    const today = new Date()
    const todayStr = today.getFullYear() + '/' + 
                     String(today.getMonth() + 1).padStart(2, '0') + '/' + 
                     String(today.getDate()).padStart(2, '0')
    
    let count = 0
    records.forEach(record => {
      const date = this.parseDate(record.trigger_time)
      if (!isNaN(date.getTime())) {
        const dateStr = date.getFullYear() + '/' + 
                       String(date.getMonth() + 1).padStart(2, '0') + '/' + 
                       String(date.getDate()).padStart(2, '0')
        if (dateStr === todayStr) {
          count++
        }
      }
    })
    this.setData({ todayCount: count })
  }
})