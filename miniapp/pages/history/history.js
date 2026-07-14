const API_BASE = 'http://114.132.168.16:5000'

Page({
  data: {
    records: [],
    loading: false,
    todayCount: 0,
    weekCount: 0
  },

  onShow() {
    this.loadHistory()
  },

  // ===== 兼容 iOS 日期格式 =====
  parseDate(dateStr) {
    if (typeof dateStr === 'string' && dateStr.includes('-')) {
      dateStr = dateStr.replace(/-/g, '/')
    }
    return new Date(dateStr)
  },

  // ===== 加载历史记录（直接显示，不转换时区） =====
  // ===== 加载历史记录（直接显示，不转换时区） =====
loadHistory() {
    const that = this
    this.setData({ loading: true })
  
    wx.request({
      url: API_BASE + '/api/history?limit=50',
      method: 'GET',
      success: (res) => {
        that.setData({ loading: false })
        if (res.data && res.data.code === 0) {
          // ✅ 直接使用服务器返回的数据，不加 8 小时
          const records = res.data.data || []
          that.setData({ records })
          that.calculateStats(records)
        }
      },
      fail: () => {
        that.setData({ loading: false })
        wx.showToast({ title: '加载失败', icon: 'none' })
      }
    })
  },
  // ===== 计算统计 =====
  calculateStats(records) {
    const today = new Date()
    const todayStr = today.getFullYear() + '/' + 
                     String(today.getMonth() + 1).padStart(2, '0') + '/' + 
                     String(today.getDate()).padStart(2, '0')
    
    const weekAgo = new Date()
    weekAgo.setDate(weekAgo.getDate() - 7)

    let todayCount = 0
    let weekCount = 0

    records.forEach(record => {
      const date = this.parseDate(record.trigger_time)
      if (!isNaN(date.getTime())) {
        const dateStr = date.getFullYear() + '/' + 
                       String(date.getMonth() + 1).padStart(2, '0') + '/' + 
                       String(date.getDate()).padStart(2, '0')
        if (dateStr === todayStr) todayCount++
        if (date >= weekAgo) weekCount++
      }
    })

    this.setData({ todayCount, weekCount })
  }
})